#include <assert.h>
#include <setjmp.h>
#include "baji_runtime.h"
#include "baji_vsync.h"

enum scenario { SOFTWARE, PANEL, DISCONNECTED, TOUCH, DRAW_FAILURE, FIRST_FAILURE };
enum { PERIOD = 17000 };
static const int64_t start = 1000000;
static enum scenario test;
static int64_t now, event_at;
static unsigned renders, lights, semaphores, edges_waited, closed, touch_reads;
static bool worker_exited, te_owned, yielding;
static StaticSemaphore_t external_event;
static void (*worker)(void *);
static void *worker_arg;
static jmp_buf blocked_input;

int64_t esp_timer_get_time(void) { return now; }
void vTaskDelay(int ticks) { now += (int64_t)ticks * 1000; }
baji_peripherals_t *baji_board_peripherals(baji_board_t *board) { return (void *)board; }
esp_err_t baji_board_set_brightness(baji_board_t *board, uint8_t value)
{
    assert(board && value == 45 && renders == 1 && !lights);
    ++lights;
    return ESP_OK;
}

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage)
{
    storage->token = 0;
    ++semaphores;
    return storage;
}
void vSemaphoreDelete(SemaphoreHandle_t s)
{
    assert(s != &external_event && semaphores);
    --semaphores;
}
int xSemaphoreGive(SemaphoreHandle_t s) { ((StaticSemaphore_t *)s)->token = 1; return pdTRUE; }

/* Exercise the real reader's complete press/release burst. Its next blocking
 * device read yields to the owner. After the owner requests shutdown, the
 * worker entry takes its stop branch without another call into the sensor. */
int xTaskCreate(void (*entry)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, TaskHandle_t *handle)
{
    (void)name; (void)stack; (void)priority; (void)handle;
    assert(test == TOUCH && !worker && lights);
    worker = entry;
    worker_arg = arg;
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { assert(!task); worker_exited = true; }
esp_err_t baji_peripherals_next_touch(baji_peripherals_t *io, uint32_t wait, baji_touch_sample_t *p)
{
    assert(io && yielding && wait);
    if (++touch_reads == 1) *p = (baji_touch_sample_t){true, 117, 132};
    else if (touch_reads == 2) { now += 2000; *p = (baji_touch_sample_t){true, 242, 140}; }
    else if (touch_reads == 3) { now += 2000; *p = (baji_touch_sample_t){true, 196, 136}; }
    else if (touch_reads == 4) { now += 3000; *p = (baji_touch_sample_t){false, 0, 0}; }
    else longjmp(blocked_input, 1);
    return ESP_OK;
}
static void deliver_event(SemaphoreHandle_t s)
{
    if (test != TOUCH) {
        assert(s == &external_event);
        xSemaphoreGive(s);
        return;
    }
    assert(worker);
    yielding = true;
    if (setjmp(blocked_input) == 0) {
        worker(worker_arg);
        assert(!"worker exited before its next blocking read");
    }
    yielding = false;
}
int xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
    StaticSemaphore_t *sem = s;
    if (!sem->token && ticks) {
        int64_t until = now + (int64_t)ticks * 1000;
        if (event_at && event_at <= until) {
            now = event_at;
            event_at = 0;
            deliver_event(s);
        } else if (ticks == portMAX_DELAY) {
            assert(test == TOUCH && renders == 2 && !worker_exited);
            worker(worker_arg);
            assert(worker_exited && sem->token);
        } else now = until;
    }
    if (!sem->token) return pdFALSE;
    sem->token = 0;
    return pdTRUE;
}

esp_err_t baji_vsync_open(void)
{
    assert(lights == 1 && !te_owned);
    te_owned = test == PANEL || test == DISCONNECTED;
    return te_owned ? ESP_OK : ESP_FAIL;
}
baji_vsync_edge_t baji_vsync_snapshot(void)
{
    assert(te_owned);
    bool pulsing = test == PANEL || (test == DISCONNECTED && now >= start+2*PERIOD);
    uint32_t seq = pulsing ? (uint32_t)((now - start) / PERIOD) : 0;
    return (baji_vsync_edge_t){seq, seq ? start+(int64_t)seq*PERIOD : 0, now};
}
esp_err_t baji_vsync_wait(uint32_t after, uint32_t timeout_ms, baji_vsync_edge_t *edge)
{
    assert(te_owned && timeout_ms && edges_waited++ < 8);
    int64_t next = start + (int64_t)(after+1)*PERIOD;
    int64_t timeout = now + 1000*(int64_t)timeout_ms;
    bool signal = (test == PANEL || (test == DISCONNECTED && after >= 2)) && next <= timeout;
    if (signal) assert(next >= now);
    now = signal ? next : timeout;
    *edge = baji_vsync_snapshot();
    return signal ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t baji_vsync_close(void)
{
    assert(te_owned);
    if (++closed == 1 && test == DISCONNECTED) return ESP_FAIL;
    te_owned = false;
    return ESP_OK;
}

static esp_err_t render(void *unused, const baji_input_t *in, int64_t time, int64_t *due)
{
    (void)unused;
    assert(time == now && renders < 4);
    unsigned index = renders++;
    if (!index) assert(now == start && !lights && !in->presses);
    *due = BAJI_FRAME_DONE;
    switch (test) {
    case SOFTWARE:
        if (index == 0) { *due = now + 1000000; event_at = now + 5000; }
        if (index == 1) {
            assert(now == start+PERIOD);
            *due = INT64_MAX;
            event_at = now + 500000;
        }
        if (index == 2) assert(now == start+PERIOD+500000);
        break;
    case PANEL:
        if (index == 0) *due = now+100000;
        if (index == 1) {
            assert(now == start+6*PERIOD && edges_waited == 1);
            *due = INT64_MAX;
            event_at = now+1;
        }
        if (index == 2) assert(now == start+7*PERIOD && edges_waited == 2);
        break;
    case DISCONNECTED:
        if (index < 3) *due = now;
        if (index == 1) {
            assert(now == start+19000 && edges_waited == 1);
            now += 4000; /* draw time is included in subsequent software pacing */
        }
        if (index == 2) assert(now == start+19000+PERIOD && edges_waited == 1);
        if (index == 3) assert(now == start+3*PERIOD && edges_waited == 2);
        break;
    case TOUCH:
        if (index == 0) { *due = INT64_MAX; event_at = now+45000; }
        else assert(now == start+52000 && in->presses == 1 && !in->touch.pressed &&
                    in->press_x == 117 && in->press_y == 132 &&
                    in->press_us == start+45000 && in->touch_us == start+52000 && in->travel == 125 &&
                    in->touch.x == 196 && in->touch.y == 136);
        break;
    case DRAW_FAILURE:
        *due = now;
        assert(time == start+index*PERIOD);
        return index ? ESP_FAIL : ESP_OK;
    case FIRST_FAILURE:
        return ESP_FAIL;
    }
    return ESP_OK;
}

int main(void)
{
    const unsigned expected[] = {3, 3, 4, 2, 4, 1};
    int board;
    for (test = SOFTWARE; test <= FIRST_FAILURE; ++test) {
        now = start;
        event_at = 0;
        renders = lights = edges_waited = closed = touch_reads = 0;
        worker = NULL;
        worker_exited = false;
        baji_board_info_t info = {.touch_status = test == TOUCH ? ESP_OK : ESP_FAIL};
        baji_runtime_config_t cfg = {.brightness = 45};
        if (test == PANEL || test == SOFTWARE) cfg.changed = &external_event;
        esp_err_t err = baji_runtime_run((void *)&board, &info, &cfg, render, NULL);
        assert(err == (test >= DRAW_FAILURE ? ESP_FAIL : ESP_OK));
        assert(renders == expected[test] && lights == (test != FIRST_FAILURE) && !te_owned);
        assert(closed == (test == PANEL ? 1u : test == DISCONNECTED ? 2u : 0u));
        assert(!semaphores && worker_exited == (test == TOUCH));
    }
    puts("Runtime deadlines, external wake, TE loss, latched short touch, callback retries and cleanup passed.");
}

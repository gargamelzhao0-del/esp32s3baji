#include "app_shim.h"
#include "baji_connectivity.h"
#include <assert.h>
#include <stdio.h>

static baji_board_t board;
static baji_agent_feed_t feed;
static unsigned stops, offline_runs;
static bool board_open;
static esp_err_t display_result;

esp_err_t baji_board_open(baji_board_t **out,baji_board_info_t *info) {
    assert(!board_open); *out = &board; *info = (baji_board_info_t){.width=466,.height=466};
    board_open = true; return ESP_OK;
}
esp_err_t baji_board_close(baji_board_t *b) {
    assert(b == &board && board_open && stops == 1); board_open = false; return ESP_OK;
}
esp_err_t baji_connectivity_start(void) { assert(board_open && !stops); return ESP_OK; }
const baji_agent_feed_t *baji_connectivity_feed(void) { return &feed; }
esp_err_t baji_connectivity_stop(void) {
    if (!stops++) { assert(board_open); return ESP_FAIL; }
    assert(!board_open); return ESP_OK;
}
esp_err_t CONNECTED_RUN(baji_board_t *b,const baji_board_info_t *info,const baji_agent_feed_t *p) {
    assert(b == &board && board_open && info->width == 466 && p == &feed);
    return display_result;
}
esp_err_t LOCAL_RUN(baji_board_t *b,const baji_board_info_t *info) {
    assert(b == &board && board_open && info->width == 466 && stops == 1);
    offline_runs++; return ESP_OK;
}
void vTaskDelay(unsigned ms) { assert(ms && !board_open); }
void app_main(void);
int main(void) {
    display_result = ESP_ERR_NO_MEM;
    app_main();
    assert(offline_runs == 1 && stops == 2);
    stops = 0;
    display_result = ESP_FAIL;
    app_main();
    assert(offline_runs == 1 && stops == 2);
    puts("Product application memory fallback and independent board teardown passed.");
}

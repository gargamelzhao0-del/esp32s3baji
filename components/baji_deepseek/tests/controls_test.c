#include "deepseek_controls.h"
#include <assert.h>
#include <stdio.h>

static dsk_controls_t ui;
static uint32_t presses;
static int64_t now;
static unsigned px, py;

static int64_t contact(bool down, unsigned x, unsigned y, unsigned elapsed)
{
    now += elapsed;
    return dsk_controls_tick(&ui, presses, px, py, down, x, y, now);
}

static void start(unsigned x, unsigned y)
{
    presses++;
    px = x;
    py = y;
    contact(true, x, y, 1000);
}

static void tap(unsigned x, unsigned y)
{
    start(x, y);
    contact(false, x, y, 1000);
}

static void swipe(unsigned x, unsigned y, unsigned end_x, unsigned end_y)
{
    start(x, y);
    contact(true, end_x, end_y, 50000);
    contact(false, end_x, end_y, 10000);
}

static void hold(bool expect)
{
    start(233, 233);
    contact(true, 233, 233, 649999);
    assert(ui.hd_mode != expect);
    contact(true, 233, 233, 1);
    assert(ui.hd_mode == expect);
    uint8_t item = ui.pixel.shown.item;
    contact(false, 233, 233, 500);
    assert(ui.pixel.shown.item == item && ui.hd_mode == expect);
}

int main(void)
{
    dsk_controls_init(&ui, 7, true, now);
    assert(!ui.hd_mode && ui.pixel.shown.item == 1);
    hold(true);
    assert(ui.hd.guides && ui.breathing && !ui.hd.gallery && ui.hd.item == 1);
    tap(240, 65);
    assert(!ui.hd.guides);
    tap(230, 400);
    assert(!ui.breathing);
    unsigned pose = ui.hd.breath * 8 + ui.breath_mix;
    assert(contact(false, 230, 400, 1500000) == INT64_MAX && ui.hd.breath * 8u + ui.breath_mix == pose);
    tap(230, 395);
    assert(ui.breathing && ui.hd.breath * 8u + ui.breath_mix == pose);
    assert(contact(false, 230, 395, 1) > now);
    bool seen[129] = {0};
    int64_t until = now + 2800000;
    while (now < until) {
        int64_t next = contact(false, 230, 395, 0);
        seen[ui.hd.breath * 8 + ui.breath_mix] = true;
        assert(next > now);
        now = next;
    }
    for (int i = 0; i < 129; i++) assert(seen[i]);
    tap(78, 220);
    assert(ui.hd.item == 6);
    tap(78, 220);
    assert(ui.hd.item == 7);
    tap(233, 220);
    assert(ui.hd.item == 1);
    for (int i = 0; i < 18; i++) {
        swipe(270, 220, 130, 215);
        assert(ui.hd.item == (2 + i) % 18);
    }
    swipe(220, 290, 220, 160);
    assert(ui.hd.gallery && ui.hd.sheet == 0);
    tap(80, 210);
    assert(ui.hd.sheet == 3);
    swipe(280, 230, 170, 235);
    assert(ui.hd.sheet == 0);
    tap(230, 210);
    assert(ui.hd.zoom == 1);
    swipe(100, 100, 390, 390);
    assert(ui.hd.pan_x == -100 && ui.hd.pan_y == -100 && ui.hd.gallery);
    swipe(410, 410, 4, 4);
    assert(ui.hd.pan_x == 100 && ui.hd.pan_y == 100 && ui.hd.gallery);
    tap(230, 210);
    assert(ui.hd.zoom == 0 && !ui.hd.pan_x && !ui.hd.pan_y);
    swipe(220, 160, 220, 280);
    assert(!ui.hd.gallery);
    hold(false);
    start(233, 233);
    contact(true, 253, 233, 200000);
    contact(true, 233, 233, 500000);
    assert(!ui.hd_mode); /* Movement cancelled long press, even after returning. */
    contact(false, 233, 233, 1000);
    puts("Dual-mode long press, 129 breathing positions, all linked poses, guides, four sheets and zoom/pan passed");
    return 0;
}

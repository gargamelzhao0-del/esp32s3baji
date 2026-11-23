#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "baji_board.h"
#include "baji_display.h"

static uint8_t covered[BAJI_DISPLAY_HEIGHT][BAJI_DISPLAY_WIDTH];

static void record_band(const baji_rect_t *band, uint8_t *pixels, void *context)
{
    (void)context;
    assert(!((band->x | band->y | band->width | band->height) & 1));
    assert(band->width && band->x + band->width <= BAJI_DISPLAY_WIDTH);
    assert(band->height && band->y + band->height <= BAJI_DISPLAY_HEIGHT);
    size_t bytes = (size_t)band->width * band->height * 2;
    assert(bytes <= BAJI_DISPLAY_MAX_TRANSFER_BYTES);
    memset(pixels, 0x55, bytes);
    for (int y = band->y; y < band->y + band->height; ++y)
        for (int x = band->x; x < band->x + band->width; ++x) {
            assert(!covered[y][x]);
            covered[y][x] = 1;
        }
}

/* Independent circle oracle on pixel centres. Include the donor's overscan
 * and the even-aligned neighbors which the production callback must repaint. */
unsigned check_round_region(baji_board_t *board, baji_rect_t area)
{
    memset(covered, 0, sizeof(covered));
    assert(baji_board_paint_round(board, area, record_band, NULL) == ESP_OK);
    unsigned count = 0;
    for (int y = 0; y < BAJI_DISPLAY_HEIGHT; ++y)
        for (int x = 0; x < BAJI_DISPLAY_WIDTH; ++x) {
            bool dirty = x >= (area.x & ~1) && x < ((area.x + area.width + 1) & ~1) &&
                         y >= (area.y & ~1) && y < ((area.y + area.height + 1) & ~1);
            int dx = 2*x - 465, dy = 2*y - 465;
            if (!dirty) assert(!covered[y][x]);
            else if (dx*dx + dy*dy <= 471*471) assert(covered[y][x]);
            count += covered[y][x];
        }
    return count;
}

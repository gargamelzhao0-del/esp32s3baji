#pragma once
#include <stddef.h>
#include <stdint.h>

enum { BAJI_BLE_FRAME_MAX = 192, BAJI_BLE_PREFIX = 2 };

typedef struct {
    uint8_t data[BAJI_BLE_FRAME_MAX];
    size_t size;
    uint8_t expected; /* Zero between messages. */
} baji_ble_fragments_t;

typedef enum { BAJI_BLE_REJECT, BAJI_BLE_MORE, BAJI_BLE_COMPLETE } baji_ble_frag_result_t;

void baji_ble_frag_reset(baji_ble_fragments_t *rx);
baji_ble_frag_result_t baji_ble_frag_push(baji_ble_fragments_t *rx, const uint8_t *chunk,
                                          size_t size);

#include "ble_fragments.h"
#include <string.h>

void baji_ble_frag_reset(baji_ble_fragments_t *rx)
{
    rx->expected = 0;
    rx->size = 0;
}

baji_ble_frag_result_t baji_ble_frag_push(baji_ble_fragments_t *rx, const uint8_t *chunk,
                                          size_t size)
{
    enum { FIRST = 1, LAST = 2 };
    if (!chunk || size <= BAJI_BLE_PREFIX || chunk[0] > (FIRST | LAST)) goto invalid;
    uint8_t flags = chunk[0], index = chunk[1];
    if (flags & FIRST) baji_ble_frag_reset(rx);
    else if (!rx->expected) goto invalid;

    size_t payload = size - BAJI_BLE_PREFIX;
    if (index != rx->expected || payload > sizeof(rx->data) - rx->size) goto invalid;
    memcpy(rx->data + rx->size, chunk + BAJI_BLE_PREFIX, payload);
    rx->size += payload;
    if (flags & LAST) {
        rx->expected = 0;
        return BAJI_BLE_COMPLETE;
    }
    /* Every chunk must contribute data, including the final one. */
    if (rx->size == sizeof(rx->data)) goto invalid;
    rx->expected = index + 1;
    return BAJI_BLE_MORE;

invalid:
    baji_ble_frag_reset(rx);
    return BAJI_BLE_REJECT;
}

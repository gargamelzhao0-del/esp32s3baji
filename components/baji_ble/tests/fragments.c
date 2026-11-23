#include "../ble_fragments.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void round_trip(size_t length, size_t att_payload)
{
    baji_ble_fragments_t rx = {0};
    uint8_t message[BAJI_BLE_FRAME_MAX], chunk[BAJI_BLE_FRAME_MAX + BAJI_BLE_PREFIX];
    for (size_t i = 0; i < length; i++) message[i] = (uint8_t)(i*31);
    uint8_t number = 0;
    for (size_t at = 0; at < length;) {
        size_t count = length - at;
        if (count > att_payload - BAJI_BLE_PREFIX) count = att_payload - BAJI_BLE_PREFIX;
        int last = at + count == length;
        chunk[0] = (number == 0) | (last << 1);
        chunk[1] = number++;
        memcpy(chunk + BAJI_BLE_PREFIX, message + at, count);
        assert(baji_ble_frag_push(&rx, chunk, count + BAJI_BLE_PREFIX) ==
               (last ? BAJI_BLE_COMPLETE : BAJI_BLE_MORE));
        at += count;
    }
    assert(rx.size == length && !memcmp(rx.data, message, length));
    assert(!rx.expected);
}

int main(void)
{
    for (size_t length = 1; length <= BAJI_BLE_FRAME_MAX; length++) {
        round_trip(length, 3);
        round_trip(length, 20); /* ATT MTU 23 */
        round_trip(length, 128);
        round_trip(length, BAJI_BLE_FRAME_MAX + BAJI_BLE_PREFIX);
    }
    baji_ble_fragments_t rx = {0};
    const uint8_t start[] = {1,0,17}, next[] = {0,1,51}, finish[] = {2,2,85};
    const uint8_t single[] = {3,0,0xaa}, invalid[] = {5,0,23}, bad_start[] = {1,1,7};
    uint8_t full[BAJI_BLE_FRAME_MAX+2] = {1};
    assert(baji_ble_frag_push(&rx, finish, sizeof finish) == BAJI_BLE_REJECT);
    assert(baji_ble_frag_push(&rx, start, sizeof start) == BAJI_BLE_MORE);
    assert(baji_ble_frag_push(&rx, finish, sizeof finish) == BAJI_BLE_REJECT); /* gap */
    assert(baji_ble_frag_push(&rx, start, sizeof start) == BAJI_BLE_MORE);
    assert(baji_ble_frag_push(&rx, next, sizeof next) == BAJI_BLE_MORE);
    assert(baji_ble_frag_push(&rx, next, sizeof next) == BAJI_BLE_REJECT); /* duplicate */
    assert(baji_ble_frag_push(&rx, start, sizeof start) == BAJI_BLE_MORE);
    baji_ble_frag_reset(&rx); /* disconnect discards the previous peer's bytes */
    assert(baji_ble_frag_push(&rx, next, sizeof next) == BAJI_BLE_REJECT);
    assert(baji_ble_frag_push(&rx, start, sizeof start) == BAJI_BLE_MORE);
    assert(baji_ble_frag_push(&rx, single, sizeof single) == BAJI_BLE_COMPLETE);
    assert(rx.size == 1 && rx.data[0] == 0xaa);
    assert(baji_ble_frag_push(&rx, single, 2) == BAJI_BLE_REJECT); /* empty payload */
    assert(baji_ble_frag_push(&rx, invalid, sizeof invalid) == BAJI_BLE_REJECT);
    assert(baji_ble_frag_push(&rx, bad_start, sizeof bad_start) == BAJI_BLE_REJECT);
    assert(baji_ble_frag_push(&rx, full, sizeof full) == BAJI_BLE_REJECT); /* impossible continuation */
    assert(baji_ble_frag_push(&rx, start, sizeof start) == BAJI_BLE_MORE);
    full[0] = 2; full[1] = 1;
    assert(baji_ble_frag_push(&rx, full, sizeof full) == BAJI_BLE_REJECT); /* overflow */
    assert(baji_ble_frag_push(&rx, single, sizeof single) == BAJI_BLE_COMPLETE);
    puts("BLE fragments: 768 bounded round trips, restart, disconnect, gap, duplicate and overflow passed.");
}

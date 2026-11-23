#include "agent_wire.h"
#include <string.h>

static unsigned u16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t)u16(p+2) << 16; }

/* Validate complete UTF-8 scalars before copying into the NUL-terminated HUD
 * label. Reject C0/C1 controls, surrogates and non-canonical encodings. */
static bool valid_label(const uint8_t *p, size_t n)
{
    size_t i = 0;
    while (i < n) {
        uint32_t c = p[i++];
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7f) return false;
            continue;
        }
        if (c < 0xc2 || c > 0xf4) return false;
        unsigned tail = c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
        if (tail > n - i) return false;
        c &= 0x3f >> tail;
        for (unsigned j = 0; j < tail; ++j) {
            if ((p[i] & 0xc0) != 0x80) return false;
            c = (c << 6) | (p[i++] & 0x3f);
        }
        if (c < 0xa0 || (tail == 2 && c < 0x800) || (tail == 3 && c < 0x10000)
            || (c >= 0xd800 && c <= 0xdfff) || c > 0x10ffff) return false;
    }
    return true;
}

static bool percent(unsigned value, uint32_t ttl, bool estimated, baji_percent_t *out)
{
    if (value == UINT16_MAX) return ttl == 0 && !estimated;
    if (value > 1000 || !ttl || ttl > 3600000) return false;
    *out = (baji_percent_t){.known=true, .estimated=estimated, .used_permille=value, .ttl_ms=ttl};
    return true;
}

bool agent_wire_decode(const uint8_t *p, size_t n, agent_message_t *out)
{
    if (!p || !out || n < BAJI_BODY_FIXED || memcmp(p, "BAS1", 4)) return false;
    unsigned flags = p[39], length = p[52];
    if (flags > 7 || length > BAJI_MODEL_MAX || n != BAJI_BODY_FIXED + length ||
        !u32(p+28) || p[36] >= BAJI_PHASE_COUNT || p[37] >= BAJI_ACTIVITY_COUNT ||
        p[38] >= BAJI_OUTCOME_COUNT) return false;
    if (!valid_label(p + BAJI_BODY_FIXED, length)) return false;
    agent_message_t msg = {.selected = (flags & 1) != 0};
    msg.snapshot = (baji_snapshot_t){.seq=u32(p+28), .identity_revision=u32(p+32),
        .phase=p[36], .activity=p[37], .outcome=p[38]};
    if (!percent(u16(p+40),u32(p+44),flags & 2,&msg.snapshot.quota) ||
        !percent(u16(p+42),u32(p+48),flags & 4,&msg.snapshot.context)) return false;
    if (!msg.selected && (length || p[36] || p[37] || p[38] ||
        msg.snapshot.quota.known || msg.snapshot.context.known)) return false;
    memcpy(msg.model, p+53, length);
    *out = msg;
    return true;
}

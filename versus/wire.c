#include "wire.h"
static void put32(uint8_t *p, uint32_t n) {
    unsigned i;
    for (i = 0; i < 4; i++) {
        p[i] = n;
        n >>= 8;
    }
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t VsCrc(const uint8_t *p, unsigned n) {
    uint16_t c = 0xFFFF;
    unsigned i;
    while (n--) {
        c ^= (uint16_t)*p++ << 8;
        for (i = 0; i < 8; i++)
            c = (c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1;
    }
    return c;
}
void VsEncode(uint8_t *p, const struct VsPacket *m) {
    unsigned i;
    uint16_t c;
    for (i = 0; i < 64; i++)
        p[i] = 0;
    p[0] = 'F';
    p[1] = 'V';
    p[2] = VS_WIRE_VERSION;
    p[3] = m->kind;
    put32(p + 4, m->content);
    put32(p + 8, m->seq);
    put32(p + 12, m->hash);
    p[16] = m->seat;
    p[17] = m->phase;
    p[18] = m->round;
    p[19] = m->outcome;
    p[20] = m->actor;
    p[21] = m->target;
    p[22] = m->x;
    p[23] = m->y;
    p[24] = m->action;
    p[25] = m->item;
    p[26] = m->cost;
    for (i = 0; i < 3; i++) {
        p[28 + 2 * i] = m->rng[i];
        p[29 + 2 * i] = m->rng[i] >> 8;
    }
    c = VsCrc(p, 62);
    p[62] = c;
    p[63] = c >> 8;
}
int VsDecode(struct VsPacket *m, const uint8_t *p, unsigned n) {
    unsigned i;
    uint16_t c;
    if (n != 64 || p[0] != 'F' || p[1] != 'V' || p[2] != VS_WIRE_VERSION || p[3] < 1 || p[3] > 7)
        return 0;
    c = VsCrc(p, 62);
    if (p[62] != (c & 255) || p[63] != (c >> 8))
        return 0;
    if (p[16] > 1 || p[17] > 1 || p[18] < 1 || p[18] > 30 || p[19] > 4 || p[25] >= 5)
        return 0;
    if (p[27])
        return 0;
    for (i = 34; i < 62; i++)
        if (p[i])
            return 0;
    m->kind = p[3];
    m->content = get32(p + 4);
    m->seq = get32(p + 8);
    m->hash = get32(p + 12);
    m->seat = p[16];
    m->phase = p[17];
    m->round = p[18];
    m->outcome = p[19];
    m->actor = p[20];
    m->target = p[21];
    m->x = p[22];
    m->y = p[23];
    m->action = p[24];
    m->item = p[25];
    m->cost = p[26];
    for (i = 0; i < 3; i++)
        m->rng[i] = p[28 + 2 * i] | (p[29 + 2 * i] << 8);
    return 1;
}

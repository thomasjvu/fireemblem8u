#ifndef VS_WIRE_H
#define VS_WIRE_H
#include <stdint.h>
#define VS_PACKET_SIZE 64
#define VS_WIRE_VERSION 1
/* All values explicitly little endian. This schema never transmits structs. */
struct VsPacket {
    uint32_t content, seq, hash;
    uint16_t rng[3];
    uint8_t kind, seat, phase, round, outcome;
    uint8_t actor, target, x, y, action, item, cost;
};
void VsEncode(uint8_t *, const struct VsPacket *);
int VsDecode(struct VsPacket *, const uint8_t *, unsigned);
uint16_t VsCrc(const uint8_t *, unsigned);
#endif

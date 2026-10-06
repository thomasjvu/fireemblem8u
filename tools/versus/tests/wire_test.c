#include "wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    struct VsPacket p = {0}, q = {0};
    unsigned i;
    uint8_t bytes[64], broken[64];
    p.kind = 3;
    p.content = 0x12345678;
    p.seq = 42;
    p.hash = 0x98abcdef;
    p.seat = 1;
    p.phase = 1;
    p.round = 30;
    p.outcome = 0;
    p.actor = 0x81;
    p.target = 2;
    p.x = 12;
    p.y = 8;
    p.action = 2;
    p.item = 4;
    p.cost = 5;
    p.rng[0] = 0x1234;
    p.rng[1] = 0xabcd;
    p.rng[2] = 0xffff;
    VsEncode(bytes, &p);
    assert(bytes[4] == 0x78 && bytes[5] == 0x56);
    assert(VsDecode(&q, bytes, 64));
    assert(q.content == p.content && q.seq == p.seq && q.hash == p.hash && q.actor == p.actor &&
           q.rng[2] == p.rng[2]);
    for (i = 0; i < 64; i++) {
        memcpy(broken, bytes, 64);
        broken[i] ^= 1;
        assert(!VsDecode(&q, broken, 64));
    }
    for (i = 0; i < 64; i++)
        assert(!VsDecode(&q, bytes, i));
    p.kind = 0;
    VsEncode(bytes, &p);
    assert(!VsDecode(&q, bytes, 64));
    p.kind = 3;
    p.seat = 2;
    VsEncode(bytes, &p);
    assert(!VsDecode(&q, bytes, 64));
    p.seat = 0;
    p.round = 0;
    VsEncode(bytes, &p);
    assert(!VsDecode(&q, bytes, 64));
    p.round = 31;
    VsEncode(bytes, &p);
    assert(!VsDecode(&q, bytes, 64));
    p.round = 1;
    p.item = 5;
    VsEncode(bytes, &p);
    assert(!VsDecode(&q, bytes, 64));
    puts("wire: round-trip, byte order, 64 corruption cases, lengths and invalid fields passed");
}

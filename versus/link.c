#include "versus.h"
#include "content.h"
#include "functions.h"
#include "hardware.h"
#include "rng.h"
#include "wire.h"
extern void *gIRQHandlers[];
static void serialInterrupt(void);
#define VS_EXECUTE 7
#define VS_SIOCNT (*(volatile u16 *)0x04000128)
#define VS_SEND (*(volatile u16 *)0x0400012A)
#define VS_RECV ((volatile u16 *)0x04000120)
#define VS_RCNT (*(volatile u16 *)0x04000134)
void VersusAbort(int code) {
    struct VersusContext *v = VS_RAM;
    v->error = code;
    v->state = VS_ERROR;
    v->outcome = VS_ABORT;
    VersusSetMessage(VS_FAILURE);
}
void VersusSetMessage(int kind) {
    struct VersusContext *v = VS_RAM;
    struct VsPacket p = {0};
    int i;
    p.kind = kind;
    p.content = VS_CONTENT_ID;
    p.seat = v->seat;
    p.phase = v->activeSeat;
    p.round = v->round;
    p.outcome = v->outcome;
    p.seq = (kind == VS_IDLE || kind == VS_HELLO) ? v->sequence : v->command.seq;
    p.hash =
        (kind == VS_COMPLETE || kind == VS_IDLE || kind == VS_HELLO) ? v->hash : v->command.preHash;
    p.actor = v->command.actor;
    p.target = v->command.target;
    p.x = v->command.x;
    p.y = v->command.y;
    p.action = v->command.type;
    p.item = v->command.itemSlot;
    p.cost = v->command.moveCount;
    for (i = 0; i < 3; i++)
        p.rng[i] = v->command.rng[i];
    {
        u8 bytes[64];
        u16 ime;
        VsEncode(bytes, &p);
        ime = REG_IME;
        REG_IME = 0;
        for (i = 0; i < 64; i++)
            v->tx[i] = bytes[i];
        REG_IME = ime;
    }
}
void VersusLinkInit(void) {
    struct VersusContext *v = VS_RAM;
    if (gIRQHandlers[7] != (void *)serialInterrupt) {
        v->savedSerial = gIRQHandlers[7];
        v->savedSerialEnable = REG_IE & INTR_FLAG_SERIAL;
    }
    SetIRQHandler(7, serialInterrupt);
    REG_IE |= INTR_FLAG_SERIAL;
    VS_RCNT = 0;
    VS_SIOCNT = 0x6003;
    VS_SEND = 0xFFFF;
    v->rxReady = v->burst = 0;
    v->seat = (VS_SIOCNT & 4) ? 1 : 0;
    v->txIndex = v->rxIndex = v->received = 0;
    v->lastRx = v->lastProgress = v->frames;
    VersusSetMessage(VS_HELLO);
}
void VersusSubmit(struct VersusCommand *c) {
    struct VersusContext *v = VS_RAM;
    if (v->state != VS_PLAY)
        return;
    if (!VersusValidate(c)) {
        VersusAbort(2);
        return;
    }
    v->command = *c;
    v->hasCommand = 1;
    v->executed = v->localDone = v->remoteDone = v->remoteReady = 0;
    v->remoteReady = v->mode == VS_LINKED;
    v->lastProgress = v->frames;
    if (v->mode == VS_HOTSEAT)
        v->state = VS_EXECUTING;
    else {
        v->state = VS_PENDING;
        VersusSetMessage(VS_ACTION);
    }
}
static void receivePacket(const struct VsPacket *p) {
    struct VersusContext *v = VS_RAM;
    int i;
    if (p->seat == v->seat)
        return;
    v->lastRx = v->frames;
    if (p->content != VS_CONTENT_ID) {
        VersusAbort(3);
        return;
    }
    if (p->kind == VS_FAILURE) {
        VersusAbort(4);
        return;
    }
    if (v->state == VS_LOBBY) {
        if (p->kind != VS_HELLO && p->kind != VS_IDLE)
            return;
        if (p->hash != v->hash || p->phase != v->activeSeat) {
            if (p->kind == VS_HELLO)
                VersusAbort(5);
            return;
        }
        if (p->kind == VS_HELLO || p->kind == VS_IDLE) {
            v->state = VS_PLAY;
            v->lastProgress = v->frames;
            VersusSetMessage(VS_IDLE);
        }
        return;
    }
    /* A next command with our completed hash also acknowledges the previous
     * command. IDLE may be replaced by ACTION before a packet boundary. */
    if (v->state == VS_DONE && p->kind == VS_ACTION && p->seq == v->command.seq + 1 &&
        p->hash == v->hash) {
        v->sequence = v->command.seq;
        v->hasCommand = 0;
        v->state = VS_PLAY;
        VersusSealState();
    }
    if (v->state == VS_PLAY && p->kind == VS_ACTION) {
        if (p->seq <= v->sequence) {
            VersusSetMessage(VS_IDLE);
            return;
        }
        struct VersusCommand c;
        if (p->seat != v->activeSeat) {
            VersusAbort(6);
            return;
        }
        c.seq = p->seq;
        c.preHash = p->hash;
        c.actor = p->actor;
        c.target = p->target;
        c.x = p->x;
        c.y = p->y;
        c.type = p->action;
        c.itemSlot = p->item;
        c.moveCount = p->cost;
        for (i = 0; i < 3; i++) {
            c.rng[i] = p->rng[i];
            if (c.rng[i] != v->confirmedRng[i]) {
                VersusAbort(7);
                return;
            }
        }
        if (!VersusValidate(&c)) {
            VersusAbort(8);
            return;
        }
        v->command = c;
        v->hasCommand = 1;
        v->executed = v->localDone = v->remoteDone = 0;
        v->remoteReady = 0;
        v->state = VS_PENDING;
        v->lastProgress = v->frames;
        VersusSetMessage(VS_READY);
        return;
    }
    if (v->state == VS_PENDING && p->seq == v->command.seq && p->hash == v->command.preHash) {
        if ((v->seat == v->activeSeat && p->kind == VS_READY) ||
            (v->seat != v->activeSeat && p->kind == VS_EXECUTE)) {
            v->state = VS_EXECUTING;
            v->lastProgress = v->frames;
            VersusSetMessage(VS_EXECUTE);
        }
    }
    if ((v->state == VS_DONE || v->state == VS_EXECUTING) && p->seq == v->command.seq &&
        (p->kind == VS_COMPLETE || p->kind == VS_IDLE)) {
        v->remoteDone = 1;
        v->remoteHash = p->hash;
        v->lastProgress = v->frames;
    }
}
void VersusFinishCommand(void) {
    struct VersusContext *v = VS_RAM;
    v->localDone = 1;
    v->hash = VersusStateHash();
    v->state = VS_DONE;
    v->lastProgress = v->frames;
    if (v->mode == VS_LINKED)
        VersusSetMessage(v->remoteReady ? VS_EXECUTE : VS_COMPLETE);
}
void VersusLinkStop(void) {
    struct VersusContext *v = VS_RAM;
    REG_IE &= ~INTR_FLAG_SERIAL;
    VS_SIOCNT = 0;
    VS_RCNT = 0x8000;
    if (v->mode == VS_LINKED && v->savedSerial) {
        SetIRQHandler(7, v->savedSerial);
        REG_IE |= v->savedSerialEnable;
    }
}
static void serialInterrupt(void) {
    struct VersusContext *v = VS_RAM;
    u16 word;
    int tag;
    /* The primary clocks one tagged byte until the secondary echoes its
     * index. The secondary follows that clock instead of advancing on
     * local video frames, which need not have the same phase. */
    word = VS_RECV[1 - v->seat];
    tag = (word >> 8) - 0x80;
    if (v->seat && tag >= 0 && tag < 64) {
        if (tag == 0 && v->txIndex != 0) {
            int i;
            for (i = 0; i < 64; i++)
                v->wireTx[i] = v->tx[i];
        }
        v->txIndex = tag;
    }
    if (!v->received) {
        int i;
        for (i = 0; i < 64; i++)
            v->wireTx[i] = v->tx[i];
        v->received = 1;
    }
    if (tag == 0)
        v->rxIndex = 0;
    if (tag >= 0 && tag < 64 && tag == v->rxIndex) {
        v->rx[v->rxIndex++] = word;
        if (v->rxIndex == 64) {
            v->rxIndex = 0;
            if (!v->rxReady) {
                int i;
                for (i = 0; i < 64; i++)
                    v->readyRx[i] = v->rx[i];
                v->rxReady = 1;
            }
        }
    }
    if (!v->seat && tag == v->txIndex) {
        v->txIndex = (v->txIndex + 1) & 63;
        if (v->txIndex == 0) {
            int i;
            for (i = 0; i < 64; i++)
                v->wireTx[i] = v->tx[i];
        }
    }
    VS_SEND = ((0x80 + v->txIndex) << 8) | v->wireTx[v->txIndex];
    if (!v->seat && v->burst) {
        v->burst--;
        VS_SIOCNT = 0x6083;
    }
}
void VersusLinkTick(void) {
    struct VersusContext *v = VS_RAM;
    struct VsPacket p;
    if (v->mode == VS_LINKED) {
        if (v->rxReady) {
            u8 bytes[64];
            u16 ime = REG_IME;
            int i;
            REG_IME = 0;
            for (i = 0; i < 64; i++)
                bytes[i] = v->readyRx[i];
            v->rxReady = 0;
            REG_IME = ime;
            if (VsDecode(&p, bytes, 64))
                receivePacket(&p);
        }
        if (!v->seat && v->frames > 60 && !(VS_SIOCNT & 0x80)) {
            v->burst = 15;
            serialInterrupt();
        }
        if (v->state != VS_ERROR && !(v->state == VS_PLAY && v->outcome) &&
            (v->frames - v->lastRx > 1800 ||
             (v->state != VS_PLAY && v->frames - v->lastProgress > 1800)))
            VersusAbort(9);
    }
    if (v->state == VS_DONE && (v->mode == VS_HOTSEAT || v->remoteDone)) {
        if (v->mode == VS_LINKED && v->hash != v->remoteHash) {
            VersusAbort(10);
            return;
        }
        v->sequence = v->command.seq;
        v->hasCommand = 0;
        v->state = VS_PLAY;
        VersusSealState();
        VersusSetMessage(VS_IDLE);
    }
}

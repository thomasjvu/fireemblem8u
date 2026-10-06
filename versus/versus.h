#ifndef FE_VERSUS_H
#define FE_VERSUS_H
#include "global.h"
#include "proc.h"
#include "bmunit.h"
#include "bmmind.h"
#define VS_MAGIC 0x56533130u
#define VS_VERSION 2
#define VS_UNITS 5
#define VS_ROUNDS 30
#define VS_MAP_SIZE 15
#define VS_RAM ((struct VersusContext *)0x0203F000)
enum { VS_HOTSEAT, VS_LINKED };
enum { VS_LOBBY, VS_PLAY, VS_PENDING, VS_EXECUTING, VS_DONE, VS_ERROR };
enum { VS_NONE, VS_BLUE_WIN, VS_RED_WIN, VS_DRAW, VS_ABORT };
enum { VS_HELLO = 1, VS_IDLE, VS_ACTION, VS_READY, VS_COMPLETE, VS_FAILURE };
enum { VS_END_PHASE = 0xF0, VS_SURRENDER = 0xF1 };
struct VersusCommand {
    u32 seq, preHash;
    u16 rng[3];
    u8 actor, target, x, y, type, itemSlot, moveCount;
};
struct VersusContext {
    u32 magic, sequence, hash, frames, lastRx, lastProgress;
    struct PlaySt savedPlay;
    struct VersusCommand command;
    struct Unit confirmedUnits[2][VS_UNITS];
    u16 confirmedRng[3];
    u8 mode, state, seat, activeSeat, opener, round, outcome, error;
    u8 chosenMode, chosenOpener, running, remoteReady, localDone, remoteDone;
    u8 tx[64], rx[64], wireTx[64], txIndex, rxIndex, received;
    u8 hasCommand, executed, phaseRequested, reserved;
    u32 remoteHash;
    u16 mapTiles[3];
    u8 readyRx[64];
    volatile u8 rxReady, burst;
    void *savedSerial;
    u16 savedSerialEnable;
};
_Static_assert(sizeof(struct VersusCommand) == 24, "command mailbox ABI");
_Static_assert(__builtin_offsetof(struct VersusContext, mode) == 0x352, "context mailbox ABI");
_Static_assert(sizeof(struct Unit) == 72, "FE8 unit ABI");
struct VersusOptions {
    u8 chosenMap, chosenBlue, chosenRed, chosenObjective;
    u8 map, blue, red, objective, victoryReason;
};
extern struct VersusOptions VersusOptions;
int VersusCanSeize(int seat, int x, int y);
extern const struct ProcCmd VersusProc[];
int VersusActive(void);
void VersusEntry(ProcPtr);
u32 VersusStateHash(void);
void VersusSealState(void);
int VersusValidate(const struct VersusCommand *);
void VersusSubmit(struct VersusCommand *);
void VersusLinkTick(void);
void VersusLinkInit(void);
void VersusLinkStop(void);
void VersusAbort(int);
void VersusSetMessage(int);
void VersusFinishCommand(void);
#endif

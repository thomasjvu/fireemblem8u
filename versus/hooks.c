#include "versus.h"
#include "bm.h"
#include "bmbattle.h"
#include "constants/items.h"
#include "bmitem.h"
#include "bmmap.h"
#include "bmsave.h"
#include "bmudisp.h"
#include "eventinfo.h"
#include "gamecontrol.h"
#include "hardware.h"
#include "mu.h"
#include "player_interface.h"
#include "playerphase.h"
#include "rng.h"
#include "uimenu.h"
struct VersusContext VersusData __attribute__((section(".bss.VersusData")));
struct Text VersusTexts[6];
static int rngAllowed;
extern u16 gRNSeeds[3];
extern void Original_WriteSuspendSave(int);
extern void Original_WriteGameSave(int);
extern void Original_StartPlayerPhaseStartTutorialEvent(void);
extern s8 Original_StartAfterUnitMovedEvent(void);
extern s8 Original_TryCallSelectEvents(void);
extern bool Original_ShouldCallEndEvent(void);
extern void Original_BattleApplyExpGains(void);
extern void Original_UnitKill(struct Unit *);
extern int Original_NextRN(void);
extern void Original_BattleGenerate(struct Unit *, struct Unit *);
extern u8 Original_OverriddenMenuAvailability(const struct MenuItemDef *, int);
extern void Original_PlayerPhase_MainIdle(ProcPtr);
extern u32 Original_ApplyUnitAction(ProcPtr);
extern void Original_PlayerPhase_FinishAction(ProcPtr);
extern void Original_TrySwitchViewedUnit(int, int);
extern s8 Original_RunPotentialWaitEvents(void);
extern u8 Original_CommandEffectEndPlayerPhase(struct MenuProc *, struct MenuItemProc *);
extern void Original_StartBattleForecastTutorialEvent(void);
extern bool Original_TryMakeCantoUnit(ProcPtr);
extern void Original_OnMain(void);
extern void VersusCompleteAction(void);
void VsWriteSuspendSave(int slot) {
    if (!VersusActive())
        Original_WriteSuspendSave(slot);
}
void VsWriteGameSave(int slot) {
    if (!VersusActive())
        Original_WriteGameSave(slot);
}
void VsTutorial(void) {
    if (!VersusActive())
        Original_StartPlayerPhaseStartTutorialEvent();
}
s8 VsMovedEvent(void) { return VersusActive() ? 0 : Original_StartAfterUnitMovedEvent(); }
s8 VsSelectEvent(void) { return VersusActive() ? 0 : Original_TryCallSelectEvents(); }
bool VsEndEvent(void) { return VersusActive() ? false : Original_ShouldCallEndEvent(); }
s8 VsWaitEvent(void) { return VersusActive() ? true : Original_RunPotentialWaitEvents(); }
void VsForecastTutorial(void) {
    if (!VersusActive())
        Original_StartBattleForecastTutorialEvent();
}
bool VsCanto(ProcPtr proc) { return VersusActive() ? false : Original_TryMakeCantoUnit(proc); }
void VsExp(void) {
    if (!VersusActive())
        Original_BattleApplyExpGains();
}
void VsKill(struct Unit *u) {
    if (VersusActive()) {
        u->curHP = 0;
        u->state |= US_DEAD | US_HIDDEN;
        u->rescue = 0;
    } else
        Original_UnitKill(u);
}
int VsNextRN(void) {
    if (VersusActive() && !rngAllowed)
        return gRNSeeds[0];
    return Original_NextRN();
}
void VsBattleGenerate(struct Unit *a, struct Unit *b) {
    int old = rngAllowed;
    rngAllowed = VersusActive() && !!(gBattleStats.config & BATTLE_CONFIG_REAL);
    Original_BattleGenerate(a, b);
    rngAllowed = old;
}
u8 VsMenuAvailability(const struct MenuItemDef *def, int number) {
    if (VersusActive()) {
        int id = def->overrideId;
        if (id >= 0x35 && id <= 0x37)
            return MENU_NOTSHOWN;
        if (id >= 0x38 && id <= 0x3D && gActiveUnit &&
            (number >= 5 || GetItemIndex(gActiveUnit->items[number]) != ITEM_VULNERARY))
            return MENU_NOTSHOWN;
        /* Whitelist the native tactical actions supported by the command validator. */
        if (id >= 0x4E && id <= 0x6D && id != 0x4F && id != 0x51 && id != 0x67 && id != 0x6B)
            return MENU_NOTSHOWN;
        if (id >= 0x6E && id <= 0x78 && id != 0x78)
            return MENU_NOTSHOWN;
    }
    return Original_OverriddenMenuAvailability(def, number);
}
static void commandFromAction(struct VersusCommand *c) {
    struct VersusContext *v = VS_RAM;
    int i;
    c->seq = v->sequence + 1;
    c->preHash = v->hash;
    for (i = 0; i < 3; i++)
        c->rng[i] = v->confirmedRng[i];
    c->actor = gActionData.subjectIndex;
    c->target = gActionData.targetIndex;
    c->x = gActionData.xMove;
    c->y = gActionData.yMove;
    c->type = gActionData.unitActionType;
    c->itemSlot = gActionData.itemSlotIndex;
    c->moveCount = gActionData.moveCount;
}
static void loadCommand(void) {
    struct VersusContext *v = VS_RAM;
    struct VersusCommand *c = &v->command;
    struct Unit *u = GetUnit(c->actor);
    gActiveUnit = u;
    gActiveUnitId = c->actor;
    gActiveUnitMoveOrigin.x = v->confirmedUnits[v->activeSeat][(c->actor & 0x3F) - 1].xPos;
    gActiveUnitMoveOrigin.y = v->confirmedUnits[v->activeSeat][(c->actor & 0x3F) - 1].yPos;
    u->xPos = c->x;
    u->yPos = c->y;
    u->state &= ~US_HIDDEN;
    gActionData.subjectIndex = c->actor;
    gActionData.targetIndex = c->target;
    gActionData.xMove = c->x;
    gActionData.yMove = c->y;
    gActionData.unitActionType = c->type;
    gActionData.itemSlotIndex = c->itemSlot;
    gActionData.moveCount = c->moveCount;
    gActionData.scriptedBattleHits = NULL;
    gBmSt.taken_action = 0;
    gBmSt.just_resumed = 0;
    LoadRNState(c->rng);
    RefreshEntityBmMaps();
}
u32 VsApplyUnitAction(ProcPtr proc) {
    struct VersusContext *v = VS_RAM;
    struct VersusCommand c;
    if (!VersusActive())
        return Original_ApplyUnitAction(proc);
    if (v->state == VS_PLAY) {
        commandFromAction(&c);
        VersusSubmit(&c);
    }
    if (v->state != VS_EXECUTING) {
        ((struct Proc *)proc)->proc_scrCur--;
        return 0;
    }
    if (v->executed) {
        VersusAbort(12);
        return 0;
    }
    v->executed = 1;
    loadCommand();
    return Original_ApplyUnitAction(proc);
}
void VsPlayerIdle(ProcPtr proc) {
    struct VersusContext *v = VS_RAM;
    if (!VersusActive()) {
        Original_PlayerPhase_MainIdle(proc);
        return;
    }
    if (v->state == VS_EXECUTING && v->hasCommand && !v->executed &&
        v->command.type < VS_END_PHASE) {
        loadCommand();
        Proc_Goto(proc, 7);
        return;
    }
    if (v->state != VS_PLAY || (v->mode == VS_LINKED && v->seat != v->activeSeat))
        return;
    if ((gKeyStatusPtr->heldKeys & (L_BUTTON | R_BUTTON)) == (L_BUTTON | R_BUTTON) &&
        (gKeyStatusPtr->newKeys & SELECT_BUTTON)) {
        struct VersusCommand c = {0};
        commandFromAction(&c);
        c.type = VS_SURRENDER;
        VersusSubmit(&c);
        return;
    }
    Original_PlayerPhase_MainIdle(proc);
}
void VsFinishAction(ProcPtr proc) {
    int s, i, j;
    Original_PlayerPhase_FinishAction(proc);
    if (!VersusActive())
        return;
    for (s = 0; s < 2; s++)
        for (i = 1; i <= 5; i++) {
            struct Unit *u = GetUnit((s ? 0x80 : 0) + i);
            u->exp = 0xFF;
            for (j = 0; j < 8; j++)
                u->ranks[j] = 181;
        }
    VersusCompleteAction();
}
u8 VsEndPhase(struct MenuProc *menu, struct MenuItemProc *item) {
    struct VersusCommand c = {0};
    if (!VersusActive())
        return Original_CommandEffectEndPlayerPhase(menu, item);
    commandFromAction(&c);
    c.type = VS_END_PHASE;
    c.actor = 0;
    c.target = 0;
    c.itemSlot = 0;
    VersusSubmit(&c);
    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6A | MENU_ACT_CLEAR;
}
void VsSwitchViewedUnit(int x, int y) {
    int i, id, faction = gPlaySt.faction;
    if (!VersusActive()) {
        Original_TrySwitchViewedUnit(x, y);
        return;
    }
    id = gBmMapUnit[y][x] & 0x3F;
    for (i = 1; i <= 5; i++)
        if (TrySetCursorOn(faction + 1 + (id + i - 1) % 5))
            return;
}
extern const struct ProcCmd gProcScr_TitleScreen[], ProcScr_SaveMenu[], VersusProc[];
/* Extras -> Link Arena, or L+R+Select from the title/save menu. */
extern void VersusAgentLegal(void);
void VsOnMain(void) {
    volatile u32 *mail = (volatile u32 *)0x0203EFF0;
    if (mail[0] == 0x56534254 ||
        (!VersusActive() && !Proc_Find(VersusProc) &&
         ((~REG_KEYINPUT) & 0x3FF) == (L_BUTTON | R_BUTTON | SELECT_BUTTON) &&
         !Proc_Find(gProc_BMapMain))) {
        struct Proc *ctrl = Proc_Find(gProcScr_GameControl);
        mail[0] = 0;
        if (ctrl && !VersusActive()) {
            while (ctrl->proc_child)
                Proc_End(ctrl->proc_child);
            ctrl->proc_lockCnt = 0;
            Proc_Goto(ctrl, 12);
        }
    }
    if (VersusActive()) {
        volatile u32 *request = (volatile u32 *)0x0203F800;
        if(request[0]==0x56534C47)VersusAgentLegal();
        if (request[0] == 0x5653434D) {
            request[0] = 0;
            if (VS_RAM->state == VS_PLAY &&
                (VS_RAM->mode == VS_HOTSEAT || VS_RAM->seat == VS_RAM->activeSeat))
                VersusSubmit((struct VersusCommand *)(request + 1));
        }
    }
    Original_OnMain();
}

extern bool Original_CheckBattleDefeatTalk(u8 pid);
bool VsCheckBattleDefeatTalk(u8 pid) {
    return VersusActive() ? false : Original_CheckBattleDefeatTalk(pid);
}

extern void Original_PidStatsAddBattleAmt(struct Unit *unit);
void VsPidStatsAddBattleAmt(struct Unit *unit) {
    if (!VersusActive())
        Original_PidStatsAddBattleAmt(unit);
}

extern void Original_PidStatsRecordBattleRes(void);
void VsPidStatsRecordBattleRes(void) {
    if (!VersusActive())
        Original_PidStatsRecordBattleRes();
}

extern void Original_PidStatsRecordDefeatInfo(u8 pid, u8 killer, int cause);
void VsPidStatsRecordDefeatInfo(u8 pid, u8 killer, int cause) {
    if (!VersusActive())
        Original_PidStatsRecordDefeatInfo(pid, killer, cause);
}

extern void Original_PidStatsAddWinAmt(u8 pid);
void VsPidStatsAddWinAmt(u8 pid) {
    if (!VersusActive())
        Original_PidStatsAddWinAmt(pid);
}

extern void Original_PidStatsRecordLoseData(u8 pid);
void VsPidStatsRecordLoseData(u8 pid) {
    if (!VersusActive())
        Original_PidStatsRecordLoseData(pid);
}

extern void Original_PidStatsAddActAmt(u8 pid);
void VsPidStatsAddActAmt(u8 pid) {
    if (!VersusActive())
        Original_PidStatsAddActAmt(pid);
}

extern void Original_PidStatsAddStatViewAmt(u8 pid);
void VsPidStatsAddStatViewAmt(u8 pid) {
    if (!VersusActive())
        Original_PidStatsAddStatViewAmt(pid);
}

extern void Original_PidStatsAddDeployAmt(u8 pid);
void VsPidStatsAddDeployAmt(u8 pid) {
    if (!VersusActive())
        Original_PidStatsAddDeployAmt(pid);
}

extern void Original_PidStatsSubFavval08(u8 pid);
void VsPidStatsSubFavval08(u8 pid) {
    if (!VersusActive())
        Original_PidStatsSubFavval08(pid);
}

extern void Original_PidStatsSubFavval100(u8 pid);
void VsPidStatsSubFavval100(u8 pid) {
    if (!VersusActive())
        Original_PidStatsSubFavval100(pid);
}

extern void Original_PidStatsAddSquaresMoved(u8 pid, int value);
void VsPidStatsAddSquaresMoved(u8 pid, int value) {
    if (!VersusActive())
        Original_PidStatsAddSquaresMoved(pid, value);
}

extern void Original_PidStatsAddExpGained(u8 pid, int value);
void VsPidStatsAddExpGained(u8 pid, int value) {
    if (!VersusActive())
        Original_PidStatsAddExpGained(pid, value);
}

extern void Original_PidStatsAddFavval(u8 pid, int value);
void VsPidStatsAddFavval(u8 pid, int value) {
    if (!VersusActive())
        Original_PidStatsAddFavval(pid, value);
}

extern void Original_GoalDisplay_Init(struct PlayerInterfaceProc *);
void VsGoalDisplay_Init(struct PlayerInterfaceProc *p) {
    /* agbcc places this Text array at 0x2C; modern GCC would place it at
     * 0x2A after PROC_HEADER. Use the verified engine ABI, not that layout. */
    struct Text *texts = (struct Text *)((u8 *)p + 0x2C);
    Original_GoalDisplay_Init(p);
    if (!VersusActive())
        return;
    ClearText(&texts[0]);
    ClearText(&texts[1]);
    Text_InsertDrawString(&texts[0], 0, TEXT_COLOR_SYSTEM_WHITE,
                          VS_RAM->activeSeat ? "Red phase" : "Blue phase");
    Text_InsertDrawNumberOrBlank(&texts[1], 8, TEXT_COLOR_SYSTEM_BLUE, VS_RAM->round);
    Text_InsertDrawString(&texts[1], 16, TEXT_COLOR_SYSTEM_WHITE, "/30 rounds");
    p->unitClock = 1;
}

extern bool Original_StartDestSelectedEvent(void);
bool VsDestEvent(void) { return VersusActive() ? false : Original_StartDestSelectedEvent(); }
extern bool Original_HandlePostActionTraps(ProcPtr);
bool VsTraps(ProcPtr p) { return VersusActive() ? true : Original_HandlePostActionTraps(p); }

_Static_assert(__builtin_offsetof(struct PlayerInterfaceProc, unitClock) == 0x44,
               "native goal-window ABI");

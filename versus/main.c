#include "versus.h"
#include "bm.h"
#include "bmio.h"
#include "bmitem.h"
#include "bmlib.h"
#include "bmmap.h"
#include "bmudisp.h"
#include "constants/characters.h"
#include "constants/classes.h"
#include "constants/items.h"
#include "constants/terrains.h"
#include "fontgrp.h"
#include "gamecontrol.h"
#include "hardware.h"
#include "mu.h"
#include "player_interface.h"
#include "playerphase.h"
#include "rng.h"
#include "uimenu.h"
#include "uiutils.h"
/* Private baseline symbols resolved by the checked build manifest. */
extern void BmMapInit(void *, u8 ***, int, int);
extern u8 sBmMapUnitPool[], sBmMapTerrainPool[], sBmMapFogPool[], sBmMapHiddenPool[],
    sBmMapOtherPool[], sBmMapMovementPool[], sBmMapRangePool[];
extern const struct MenuDef VersusLobbyMenu, VersusResultMenu;
extern struct Text VersusTexts[];
extern const struct ProcCmd VersusTransportProc[];
struct VersusOptions VersusOptions;
struct VsRoster {
    u8 classId, item, hp, power, speed, defense, resistance;
};
#include "catalog_counts.h"
#include "catalog.h"
int VersusCanSeize(int seat, int x, int y) {
    return VersusOptions.objective != 0 && x == 7 && y == (seat ? 13 : 1);
}
int VersusActive(void) { return VS_RAM->magic == VS_MAGIC && VS_RAM->running; }
static void zero(void *p, unsigned n) {
    u8 *b = p;
    while (n--)
        *b++ = 0;
}
static void panel(void) {
    BG_Fill(gBG0TilemapBuffer, 0);
    BG_Fill(gBG1TilemapBuffer, 0);
    ResetTextFont();
    LoadUiFrameGraphics();
    DrawUiFrame(gBG1TilemapBuffer, 1, 1, 28, 18, 0, 0);
    BG_EnableSyncByMask(BG0_SYNC_BIT | BG1_SYNC_BIT);
}
static void line(int row, const char *text) {
    InitText(&VersusTexts[row], 13);
    PutDrawText(&VersusTexts[row], gBG0TilemapBuffer + 32 * (3 + row * 2) + 3,
                TEXT_COLOR_SYSTEM_WHITE, 0, 13, text);
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}
void VersusOpenerText(void) {
    static const char *goals[3] = {"Elimination", "Seizure", "Either"};
    line(1, mapNames[VersusOptions.chosenMap]);
    line(2, goals[VersusOptions.chosenObjective]);
    line(3, bluePartyNames[VersusOptions.chosenBlue]);
    line(4, redPartyNames[VersusOptions.chosenRed]);
    line(5, VS_RAM->chosenOpener ? "Red opens" : "Blue opens");
}
static void lobby(ProcPtr proc) {
    struct VersusContext *v = VS_RAM;
    struct PlaySt saved = gPlaySt;
    zero(v, sizeof(*v));
    v->magic = VS_MAGIC;
    v->savedPlay = saved;
    v->round = 1;
    zero(&VersusOptions, sizeof(VersusOptions));
    SetupBackgrounds(NULL);
    ResetKeyStatus(gKeyStatusPtr);
    ResetText();
    panel();
    line(0, "Fire Emblem Versus");
    line(1, "Five units. Real tactical battles.");
    VersusOpenerText();
    StartMenu(&VersusLobbyMenu, proc);
}
static void fixtureMap(void) {
    struct VersusContext *v = VS_RAM;
    int x, y, k;
    u16 tile[6] = {6 * 4, 881 * 4, 868 * 4, 724 * 4, 877 * 4, 910 * 4};
    /* A full three-column native castle, with the southern castle facing north.
     * The final six metatile slots are private to Versus. Each consists of four
     * 8x8 characters; reverse the rows and flip each character vertically.
     */
    u16 *config = (u16 *)(gTilesetTerrainLookup - 0x2000);
    static const u16 castle[6] = {877, 878, 879, 909, 910, 911};
    for (k = 0; k < 6; k++) {
        u16 *source = config + castle[k] * 4;
        u16 *dest = config + (960 + k) * 4;
        dest[0] = source[2] ^ 0x0800;
        dest[1] = source[3] ^ 0x0800;
        dest[2] = source[0] ^ 0x0800;
        dest[3] = source[1] ^ 0x0800;
        gTilesetTerrainLookup[960 + k] = gTilesetTerrainLookup[castle[k]];
    }
    for (k = 0; k < 3; k++)
        v->mapTiles[k] = tile[k];
    gBmMapSize.x = gBmMapSize.y = VS_MAP_SIZE;
    gBmMapBuffer[0] = VS_MAP_SIZE | (VS_MAP_SIZE << 8);
    for (y = 0; y < VS_MAP_SIZE; y++)
        for (x = 0; x < VS_MAP_SIZE; x++) {
            const u8(*map)[15] = scenarioTiles[VersusOptions.map];
            u16 chosen;
            k = map[y][x];
            chosen = tile[k];
            if (k == 1) {
                int left = x > 0 && map[y][x - 1] == 1;
                int right = x < 14 && map[y][x + 1] == 1;
                chosen = (left && right ? 881 : right ? 880 : 882) * 4;
            } else if (k == 2) {
                chosen = (x > 0 && map[y][x - 1] == 2 ? 869 : 868) * 4;
            } else if (k == 3) {
                int horizontal = (x > 0 && map[y][x - 1] == 3) || (x < 14 && map[y][x + 1] == 3);
                int vertical = (y > 0 && map[y - 1][x] == 3) || (y < 14 && map[y + 1][x] == 3);
                chosen = (horizontal && vertical ? 724 : horizontal ? 788 : 762) * 4;
            } else if (k >= 4) {
                int column = x - 6;
                if (y <= 1)
                    chosen = castle[y * 3 + column] * 4;
                else
                    chosen = (960 + (14 - y) * 3 + column) * 4;
            }
            gBmMapBuffer[1 + y * VS_MAP_SIZE + x] = chosen;
        }
    BmMapInit(sBmMapUnitPool, &gBmMapUnit, 15, 15);
    BmMapInit(sBmMapTerrainPool, &gBmMapTerrain, 15, 15);
    BmMapInit(sBmMapFogPool, &gBmMapFog, 15, 15);
    BmMapInit(sBmMapHiddenPool, &gBmMapHidden, 15, 15);
    BmMapInit(sBmMapOtherPool, &gBmMapOther, 15, 15);
    BmMapInit(sBmMapMovementPool, &gBmMapMovement, 15, 15);
    BmMapInit(sBmMapRangePool, &gBmMapRange, 15, 15);
    BmMapFill(gBmMapUnit, 0);
    BmMapFill(gBmMapHidden, 0);
    BmMapFill(gBmMapFog, 1);
    InitBaseTilesBmMap();
    RefreshTerrainBmMap();
    gBmSt.cameraMax.x = 0;
    gBmSt.cameraMax.y = 80;
}
static void armies(void) {
    static const u8 chars[5] = {CHARACTER_EIRIKA, CHARACTER_GARCIA, CHARACTER_NEIMI, CHARACTER_LUTE,
                                CHARACTER_MOULDER};
    static const u8 deploymentX[5] = {3, 5, 6, 9, 11};
    int s, i, j;
    InitUnits();
    for (s = 0; s < 2; s++)
        for (i = 0; i < 5; i++) {
            struct Unit *u = GetUnit((s ? 0x80 : 0) + i + 1);
            const struct VsRoster *r = &roster[s ? VersusOptions.red : VersusOptions.blue][i];
            ClearUnit(u);
            u->pCharacterData = GetCharacterData(chars[i]);
            u->pClassData = GetClassData(r->classId);
            u->level = 20;
            u->exp = 0xFF;
            u->xPos = deploymentX[i];
            u->yPos = s ? 2 : 12;
            u->maxHP = u->curHP = r->hp;
            u->pow = r->power;
            u->skl = 10;
            u->spd = r->speed;
            u->lck = 7;
            u->def = r->defense;
            u->res = r->resistance;
            for (j = 0; j < 8; j++)
                u->ranks[j] = 181;
            u->items[0] = MakeNewItem(r->item);
            u->items[1] = MakeNewItem(ITEM_VULNERARY);
        }
}
static void begin(ProcPtr proc) {
    struct VersusContext *v = VS_RAM;
    if (v->chosenMode == 2) {
        Proc_Goto(proc, 3);
        return;
    }
    Proc_EndEach(VersusTransportProc);
    v->hasCommand = v->executed = v->remoteReady = v->localDone = v->remoteDone = 0;
    if (VersusOptions.chosenMap >= VS_MAP_COUNT || VersusOptions.chosenBlue >= VS_PARTY_COUNT ||
        VersusOptions.chosenRed >= VS_PARTY_COUNT ||
        VersusOptions.chosenObjective >= VS_OBJECTIVE_COUNT) {
        VersusAbort(5);
        return;
    }
    VersusOptions.map = VersusOptions.chosenMap;
    VersusOptions.blue = VersusOptions.chosenBlue;
    VersusOptions.red = VersusOptions.chosenRed;
    VersusOptions.objective = VersusOptions.chosenObjective;
    VersusOptions.victoryReason = 0;
    v->mode = v->chosenMode;
    v->opener = v->chosenOpener;
    v->activeSeat = v->opener;
    v->round = 1;
    v->sequence = 0;
    v->outcome = 0;
    v->error = 0;
    gPlaySt.chapterIndex = 0;
    gPlaySt.chapterStateBits = PLAY_FLAG_EXTRA_MAP;
    gPlaySt.config.animationType = 1;
    gPlaySt.config.disableAutoEndTurns = 1;
    gPlaySt.chapterVisionRange = 0;
    gPlaySt.chapterWeatherId = 0;
    gPlaySt.tutorial_counter = 0;
    StartBattleMap(NULL);
    ReadGameSaveCoreGfx();
    InitSystemTextFont();
    fixtureMap();
    armies();
    gPlaySt.faction = v->activeSeat ? FACTION_RED : FACTION_BLUE;
    gPlaySt.chapterTurnNumber = 1;
    gPlaySt.chapterVisionRange = 0;
    gBmSt.gameStateBits = 0;
    gBmSt.camera.y = v->activeSeat ? 0 : 80;
    BMapVSync_Start();
    Proc_Start(gProc_MapTask, PROC_TREE_4);
    RefreshEntityBmMaps();
    RenderBmMap();
    RefreshUnitSprites();
    SetCursorMapPosition(6, v->activeSeat ? 2 : 12);
    InitRN(7);
    v->running = 1;
    VersusSealState();
    v->state = v->mode == VS_LINKED ? VS_LOBBY : VS_PLAY;
    if (v->mode == VS_LINKED)
        VersusLinkInit();
    StartMidFadeFromBlack();
    Proc_Start(VersusTransportProc, PROC_TREE_3);
}
static int living(int seat) {
    int i, n = 0;
    for (i = 1; i <= 5; i++) {
        struct Unit *u = GetUnit((seat ? 0x80 : 0) + i);
        if (u->curHP > 0 && !(u->state & (US_DEAD | US_NOT_DEPLOYED)))
            n++;
    }
    return n;
}
static void outcome(void) {
    struct VersusContext *v = VS_RAM;
    int a = living(0), b = living(1);
    if (v->outcome)
        return;
    if (a && b)
        return;
    VersusOptions.victoryReason = VersusOptions.objective == 1 ? 4 : 1;
    if (!a && !b)
        v->outcome = VS_DRAW;
    else if (!a)
        v->outcome = VS_RED_WIN;
    else if (!b)
        v->outcome = VS_BLUE_WIN;
}
void VersusAdvancePhase(void) {
    struct VersusContext *v = VS_RAM;
    int i;
    outcome();
    if (v->outcome)
        return;
    v->activeSeat ^= 1;
    if (v->activeSeat == v->opener) {
        if (v->round == VS_ROUNDS) {
            VersusOptions.victoryReason = 3;
            v->outcome = VS_DRAW;
            return;
        }
        v->round++;
    }
    gPlaySt.faction = v->activeSeat ? 0x80 : 0;
    gPlaySt.chapterTurnNumber = v->round;
    for (i = 1; i <= 5; i++) {
        struct Unit *u = GetUnit(gPlaySt.faction + i);
        int heal;
        u->state &= ~(US_UNSELECTABLE | US_HAS_MOVED | US_CANTOING);
        if (u->curHP <= 0 || u->state & (US_DEAD | US_NOT_DEPLOYED))
            continue;
        heal = GetTerrainHealAmount(gBmMapTerrain[u->yPos][u->xPos]) * u->maxHP / 100;
        u->curHP += heal;
        if (u->curHP > u->maxHP)
            u->curHP = u->maxHP;
    }
    RefreshEntityBmMaps();
    RefreshUnitSprites();
    SetCursorMapPosition(6, v->activeSeat ? 2 : 12);
}
void VersusCompleteAction(void) {
    if (VS_RAM->command.type == UNIT_ACTION_SEIZE) {
        VS_RAM->outcome = VS_RAM->activeSeat ? VS_RED_WIN : VS_BLUE_WIN;
        VersusOptions.victoryReason = 2;
    }
    outcome();
    VersusFinishCommand();
}
static void transport(ProcPtr proc) {
    struct VersusContext *v = VS_RAM;
    (void)proc;
    v->frames++;
    VersusLinkTick();
    if (v->state == VS_EXECUTING && !v->executed &&
        (v->command.type == VS_END_PHASE || v->command.type == VS_SURRENDER)) {
        v->executed = 1;
        EndPlayerPhaseSideWindows();
        Proc_EndEach(gProcScr_PlayerPhase);
        if (v->command.type == VS_SURRENDER) {
            VersusOptions.victoryReason = 5;
            v->outcome = v->activeSeat ? VS_BLUE_WIN : VS_RED_WIN;
        } else
            VersusAdvancePhase();
        VersusFinishCommand();
    }
}
const struct ProcCmd VersusTransportProc[] = {PROC_NAME("VS_TRANSPORT"), PROC_REPEAT(transport),
                                              PROC_END};
static void phase(ProcPtr proc) {
    struct VersusContext *v = VS_RAM;
    if (v->state == VS_ERROR || (v->outcome && v->state == VS_PLAY)) {
        EndPlayerPhaseSideWindows();
        Proc_EndEach(gProcScr_PlayerPhase);
        Proc_Goto(proc, 2);
        return;
    }
    if (v->state != VS_PLAY)
        return;
    if (!Proc_Find(gProcScr_PlayerPhase))
        Proc_Start(gProcScr_PlayerPhase, PROC_TREE_2);
}
static const char *errorMessage(int e) {
    switch (e) {
    case 3:
        return "Different patch versions.";
    case 5:
        return "Opening army or map mismatch.";
    case 7:
        return "Random state mismatch.";
    case 9:
        return "Link timed out.";
    case 10:
        return "Battle state mismatch.";
    case 2:
    case 8:
        return "Invalid battle command.";
    case 4:
        return "Other player aborted.";
    default:
        return "Command execution error.";
    }
}
static void results(ProcPtr proc) {
    struct VersusContext *v = VS_RAM;
    EndAllMus();
    if (v->mode == VS_HOTSEAT)
        Proc_EndEach(VersusTransportProc);
    Proc_EndEach(gProc_MapTask);
    BMapVSync_End();
    v->running = 0;
    if (v->mode == VS_HOTSEAT)
        VersusLinkStop(); /* serial disabled */
    SetupBackgrounds(NULL);
    ResetKeyStatus(gKeyStatusPtr);
    ResetText();
    panel();
    line(0, "Fire Emblem Versus");
    line(1, v->outcome == VS_BLUE_WIN  ? "Blue army wins!"
            : v->outcome == VS_RED_WIN ? "Red army wins!"
            : v->outcome == VS_DRAW    ? "Draw: round limit or mutual defeat."
                                       : "Match aborted: link or state error.");
    line(2, v->error ? errorMessage(v->error) : "Rematch swaps the opening army.");
    StartMenu(&VersusResultMenu, proc);
}
static void cleanup(ProcPtr proc) {
    struct VersusContext *v = VS_RAM;
    (void)proc;
    gPlaySt = v->savedPlay;
    InitUnits();
    ResetMenuOverrides();
    Proc_EndEach(VersusTransportProc);
    VersusLinkStop();
    v->running = 0;
    v->magic = 0;
}
const struct ProcCmd VersusProc[] = {PROC_NAME("FIRE_EMBLEM_VERSUS"),
                                     PROC_CALL(lobby),
                                     PROC_YIELD,
                                     PROC_LABEL(0),
                                     PROC_CALL(begin),
                                     PROC_REPEAT(WaitForFade),
                                     PROC_LABEL(1),
                                     PROC_REPEAT(phase),
                                     PROC_GOTO(1),
                                     PROC_LABEL(2),
                                     PROC_CALL(results),
                                     PROC_YIELD,
                                     PROC_GOTO(0),
                                     PROC_LABEL(3),
                                     PROC_CALL(cleanup),
                                     PROC_END};
extern void sub_8009A84(ProcPtr);
void VersusEntry(ProcPtr parent) {
    /* Reuse the game controller's presentation teardown. Intro/save-menu
     * orphan processes must not keep changing BG registers during a match. */
    sub_8009A84(parent);
    ClearTileRigistry();
    Proc_StartBlocking(VersusProc, parent);
}

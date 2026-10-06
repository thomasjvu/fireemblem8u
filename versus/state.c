#include "versus.h"
#include "bmidoten.h"
#include "bmitem.h"
#include "bmmap.h"
#include "constants/items.h"
#include "rng.h"
static u32 byteHash(u32 h, u8 b) { return (h ^ b) * 16777619u; }
static u32 wordHash(u32 h, u32 x) {
    int i;
    for (i = 0; i < 4; i++) {
        h = byteHash(h, x & 255);
        x >>= 8;
    }
    return h;
}
u32 VersusStateHash(void) {
    struct VersusContext *v = VS_RAM;
    u32 h = 2166136261u;
    u16 rn[3];
    int s, i, j;
    h = wordHash(h, VS_VERSION);
    h = wordHash(h, v->round);
    h = wordHash(h, v->activeSeat);
    h = wordHash(h, v->outcome);
    for (s = 0; s < 2; s++)
        for (i = 1; i <= VS_UNITS; i++) {
            struct Unit *u = GetUnit((s ? FACTION_RED : FACTION_BLUE) + i);
            h = byteHash(h, u->pCharacterData->number);
            h = byteHash(h, u->pClassData->number);
            h = wordHash(h, u->state & ~(US_HIDDEN));
            h = byteHash(h, u->xPos);
            h = byteHash(h, u->yPos);
            h = byteHash(h, u->level);
            h = byteHash(h, u->exp);
            h = byteHash(h, u->maxHP);
            h = byteHash(h, u->curHP);
            h = byteHash(h, u->pow);
            h = byteHash(h, u->skl);
            h = byteHash(h, u->spd);
            h = byteHash(h, u->def);
            h = byteHash(h, u->res);
            h = byteHash(h, u->lck);
            h = byteHash(h, u->conBonus);
            h = byteHash(h, u->movBonus);
            h = byteHash(h, u->rescue);
            h = byteHash(h, u->statusIndex);
            h = byteHash(h, u->statusDuration);
            h = byteHash(h, u->barrierDuration);
            for (j = 0; j < 5; j++)
                h = wordHash(h, u->items[j]);
            for (j = 0; j < 8; j++)
                h = byteHash(h, u->ranks[j]);
        }
    StoreRNState(rn);
    for (i = 0; i < 3; i++)
        h = wordHash(h, rn[i]);
    for (i = 0; i < VS_MAP_SIZE; i++)
        for (j = 0; j < VS_MAP_SIZE; j++)
            h = byteHash(h, gBmMapTerrain[i][j]);
    return h;
}
void VersusSealState(void) {
    int s, i;
    struct VersusContext *v = VS_RAM;
    for (s = 0; s < 2; s++)
        for (i = 0; i < VS_UNITS; i++)
            v->confirmedUnits[s][i] = *GetUnit((s ? 0x80 : 0) + i + 1);
    StoreRNState(v->confirmedRng);
    v->hash = VersusStateHash();
}
int VersusValidate(const struct VersusCommand *c) {
    struct VersusContext *v = VS_RAM;
    struct Unit *u, *target;
    int slot, dist, item, sx, sy, cost;
    if (c->seq != v->sequence + 1 || c->preHash != v->hash)
        return 0;
    if (c->type == VS_END_PHASE || c->type == VS_SURRENDER)
        return 1;
    slot = (c->actor & 0x3F) - 1;
    if (slot < 0 || slot >= VS_UNITS || ((c->actor & 0xC0) != (v->activeSeat ? 0x80 : 0)))
        return 0;
    u = GetUnit(c->actor);
    /* Validate from the confirmed position, not the local movement preview. */
    sx = u->xPos;
    sy = u->yPos;
    u->xPos = v->confirmedUnits[v->activeSeat][slot].xPos;
    u->yPos = v->confirmedUnits[v->activeSeat][slot].yPos;
    if (v->confirmedUnits[v->activeSeat][slot].state &
        (US_DEAD | US_NOT_DEPLOYED | US_UNSELECTABLE | US_HAS_MOVED)) {
        u->xPos = sx;
        u->yPos = sy;
        return 0;
    }
    if (c->x >= VS_MAP_SIZE || c->y >= VS_MAP_SIZE) {
        u->xPos = sx;
        u->yPos = sy;
        return 0;
    }
    RefreshEntityBmMaps();
    GenerateUnitMovementMap(u);
    cost = gBmMapMovement[c->y][c->x];
    u->xPos = sx;
    u->yPos = sy;
    RefreshEntityBmMaps();
    if (cost > UNIT_MOV(u) || (gBmMapUnit[c->y][c->x] && gBmMapUnit[c->y][c->x] != c->actor))
        return 0;
    if (c->type == UNIT_ACTION_WAIT)
        return 1;
    if (c->itemSlot >= 5)
        return 0;
    item = u->items[c->itemSlot];
    if (!item)
        return 0;
    if (c->type == UNIT_ACTION_USE_ITEM)
        return GetItemIndex(item) == ITEM_VULNERARY && u->curHP < u->maxHP;
    if ((c->target & 0x40) || (c->target & 0x3F) == 0 || (c->target & 0x3F) > VS_UNITS)
        return 0;
    target = GetUnit(c->target);
    if (!target || !UNIT_IS_VALID(target) || target->state & (US_DEAD | US_NOT_DEPLOYED))
        return 0;
    dist = ABS((int)c->x - target->xPos) + ABS((int)c->y - target->yPos);
    if (c->type == UNIT_ACTION_COMBAT)
        return ((c->target ^ c->actor) & 0x80) && CanUnitUseWeapon(u, item) &&
               dist >= GetItemMinRange(item) && dist <= GetItemMaxRange(item);
    if (c->type == UNIT_ACTION_STAFF)
        return target != u && !((c->target ^ c->actor) & 0x80) &&
               GetItemIndex(item) == ITEM_STAFF_HEAL && CanUnitUseStaff(u, item) && dist == 1 &&
               target->curHP < target->maxHP;
    return 0;
}

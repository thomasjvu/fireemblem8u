#include "versus.h"
#include "bmitem.h"
#include "bmmap.h"
#include "bmidoten.h"
#include "constants/items.h"
/* Paginated read-only legal actions for agents. Never advances game RNG. */
static void emit(struct VersusCommand *c, int first, int *total, int *count) {
    if (*total >= first && *count < 64) {
        ((struct VersusCommand *)0x0203F900)[(*count)++] = *c;
    }
    (*total)++;
}
void VersusAgentLegal(void) {
    volatile u32 *mail = (volatile u32 *)0x0203F800;
    struct VersusContext *v = VS_RAM;
    struct VersusCommand c = {0};
    int first = mail[1], total = 0, count = 0, i, x, y, j, s, t;
    c.seq = v->sequence + 1;
    c.preHash = v->hash;
    for (i = 0; i < 3; i++)
        c.rng[i] = v->confirmedRng[i];
    if (v->state != VS_PLAY || v->outcome || v->seat != v->activeSeat)
        goto done;
    for (i = 1; i <= VS_UNITS; i++) {
        struct Unit *u = GetUnit(v->activeSeat * 0x80 + i);
        if (u->curHP <= 0 ||
            u->state & (US_DEAD | US_NOT_DEPLOYED | US_UNSELECTABLE | US_HAS_MOVED))
            continue;
        c.actor = u->index;
        RefreshEntityBmMaps();
        GenerateUnitMovementMap(u);
        for (y = 0; y < VS_MAP_SIZE; y++)
            for (x = 0; x < VS_MAP_SIZE; x++) {
                int cost = gBmMapMovement[y][x];
                if (cost > UNIT_MOV(u) || (gBmMapUnit[y][x] && gBmMapUnit[y][x] != c.actor))
                    continue;
                c.x = x;
                c.y = y;
                c.moveCount = cost;
                c.target = 0;
                c.itemSlot = 0;
                c.type = UNIT_ACTION_WAIT;
                emit(&c, first, &total, &count);
                if (VersusCanSeize(v->activeSeat, x, y)) {
                    c.type = UNIT_ACTION_SEIZE;
                    emit(&c, first, &total, &count);
                }
                for (j = 0; j < 5; j++) {
                    int item = u->items[j];
                    if (!item)
                        continue;
                    c.itemSlot = j;
                    if (GetItemIndex(item) == ITEM_VULNERARY && u->curHP < u->maxHP) {
                        c.type = UNIT_ACTION_USE_ITEM;
                        c.target = 0;
                        emit(&c, first, &total, &count);
                    }
                    for (s = 0; s < 2; s++)
                        for (t = 1; t <= 5; t++) {
                            struct Unit *target = GetUnit(s * 0x80 + t);
                            int dist;
                            if (target->curHP <= 0 || target->state & (US_DEAD | US_NOT_DEPLOYED))
                                continue;
                            dist = ABS(x - target->xPos) + ABS(y - target->yPos);
                            c.target = target->index;
                            if (s != v->activeSeat && CanUnitUseWeapon(u, item) &&
                                dist >= GetItemMinRange(item) && dist <= GetItemMaxRange(item)) {
                                c.type = UNIT_ACTION_COMBAT;
                                emit(&c, first, &total, &count);
                            }
                            if (target != u && s == v->activeSeat &&
                                GetItemIndex(item) == ITEM_STAFF_HEAL && CanUnitUseStaff(u, item) &&
                                dist == 1 && target->curHP < target->maxHP) {
                                c.type = UNIT_ACTION_STAFF;
                                emit(&c, first, &total, &count);
                            }
                        }
                }
            }
    }
    c.actor = c.target = c.x = c.y = c.itemSlot = c.moveCount = 0;
    c.type = VS_END_PHASE;
    emit(&c, first, &total, &count);
    c.type = VS_SURRENDER;
    emit(&c, first, &total, &count);
done:
    RefreshEntityBmMaps();
    mail[2] = count;
    mail[3] = total;
    mail[0] = 0;
}

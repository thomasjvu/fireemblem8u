#include "versus.h"
#include "uimenu.h"
extern void VersusOpenerText(void);
static u8 choose(struct MenuProc *m, struct MenuItemProc *i) {
    (void)m;
    VS_RAM->chosenMode = i->itemNumber;
    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6A | MENU_ACT_CLEAR;
}
static u8 cancel(struct MenuProc *m, struct MenuItemProc *i) {
    (void)m;
    (void)i;
    VS_RAM->chosenMode = 2;
    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6B | MENU_ACT_CLEAR;
}
static u8 opener(struct MenuProc *m, struct MenuItemProc *i) {
    (void)m;
    (void)i;
    VS_RAM->chosenOpener ^= 1;
    VersusOpenerText();
    return MENU_ACT_SND6A;
}
static u8 rematch(struct MenuProc *m, struct MenuItemProc *i) {
    (void)m;
    (void)i;
    VS_RAM->chosenOpener ^= 1;
    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6A | MENU_ACT_CLEAR;
}
static u8 option(struct MenuProc *m, struct MenuItemProc *i) {
    u8 *choices = &VersusOptions.chosenMap;
    (void)m;
    choices[i->itemNumber - 4] = (choices[i->itemNumber - 4] + 1) % 3;
    VersusOpenerText();
    return MENU_ACT_SND6A;
}
static const struct MenuItemDef lobbyItems[] = {
    {.name = "Hotseat battle", .isAvailable = MenuAlwaysEnabled, .onSelected = choose},
    {.name = "Linked battle", .isAvailable = MenuAlwaysEnabled, .onSelected = choose},
    {.name = "Return", .isAvailable = MenuAlwaysEnabled, .onSelected = cancel},
    {.name = "Swap opening army", .isAvailable = MenuAlwaysEnabled, .onSelected = opener},
    {.name = "Change map", .isAvailable = MenuAlwaysEnabled, .onSelected = option},
    {.name = "Blue party", .isAvailable = MenuAlwaysEnabled, .onSelected = option},
    {.name = "Red party", .isAvailable = MenuAlwaysEnabled, .onSelected = option},
    {.name = "Victory rule", .isAvailable = MenuAlwaysEnabled, .onSelected = option},
    {0}};
static const struct MenuItemDef resultItems[] = {
    {.name = "Rematch", .isAvailable = MenuAlwaysEnabled, .onSelected = rematch},
    {.name = "Return", .isAvailable = MenuAlwaysEnabled, .onSelected = cancel},
    {0}};
const struct MenuDef VersusLobbyMenu = {
    .rect = {17, 5, 12, 0}, .menuItems = lobbyItems, .onBPress = cancel};
const struct MenuDef VersusResultMenu = {
    .rect = {3, 11, 24, 0}, .menuItems = resultItems, .onBPress = cancel};

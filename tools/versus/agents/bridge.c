/* Two real mGBA cores using mGBA's multiplayer serial driver. */
#include <mgba/core/core.h>
#include <mgba/core/lockstep.h>
#include <mgba/core/log.h>
#include <mgba/core/thread.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/sio/lockstep.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
struct Player {
    struct mCoreThread thread;
    struct mLockstepThreadUser user;
    struct GBASIOLockstepDriver driver;
    mColor pixels[240 * 160];
    atomic_uint frame, seq, state, error, seat, active, hash;
    unsigned uiStep, kind, page;
    unsigned char command[24];
    char response[8192];
    atomic_uint request;
};
static struct Player players[2];
static int redOpener;
static void stopped(struct mLockstepUser *u) { (void)u; }
static void quiet(struct mLogger *l, int c, enum mLogLevel v, const char *fmt, va_list args) {
    (void)l;
    (void)c;
    (void)v;
    (void)fmt;
    (void)args;
}
static struct mLogger logger = {.log = quiet};
static unsigned terrainAddress;
static void hexbytes(struct mCore *c, unsigned addr, unsigned n, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < n; i++) {
        unsigned v = c->busRead8(c, addr + i);
        out[2 * i] = digits[v >> 4];
        out[2 * i + 1] = digits[v & 15];
    }
    out[2 * n] = 0;
}
static void callback(struct mCoreThread *t) {
    struct Player *p = t->userData;
    struct mCore *c = t->core;
    unsigned f = c->frameCounter(c), keys = 0;
    if (f == 120)
        c->busWrite32(c, 0x0203eff0, 0x56534254);
    if (f >= 200 && f < 203)
        keys = 0x80;
    if (f == 220 && redOpener)
        c->busWrite8(c, 0x0203f35b, 1);
    if (f >= 240 && f < 243)
        keys = 1;
    c->setKeys(c, keys);
    unsigned req = atomic_load(&p->request);
    if (req == 1) {
        unsigned state = c->busRead8(c, 0x0203f353), seat = c->busRead8(c, 0x0203f354),
                 active = c->busRead8(c, 0x0203f355);
        if (p->kind == 0) {
            char units[1441], terrain[451], rng[13];
            hexbytes(c, 0x0203f07c, 720, units);
            hexbytes(c, 0x0203f34c, 6, rng);
            unsigned rows = c->busRead32(c, terrainAddress);
            for (unsigned y = 0; y < 15; y++)
                hexbytes(c, c->busRead32(c, rows + 4 * y), 15, terrain + 30 * y);
            snprintf(
                p->response, sizeof(p->response),
                "{\"seq\":%u,\"hash\":%u,\"state\":%u,\"seat\":%u,\"active\":%u,\"round\":%u,"
                "\"outcome\":%u,\"error\":%u,\"units\":\"%s\",\"terrain\":\"%s\",\"rng\":\"%s\"}",
                c->busRead32(c, 0x0203f004), c->busRead32(c, 0x0203f008), state, seat, active,
                c->busRead8(c, 0x0203f357), c->busRead8(c, 0x0203f358), c->busRead8(c, 0x0203f359),
                units, terrain, rng);
            atomic_store(&p->request, 2);
        } else if (state != 1 || seat != active || c->busRead8(c, 0x0203f358)) {
            strcpy(p->response, "{\"error\":\"not_your_turn\"}");
            atomic_store(&p->request, 2);
        } else if (p->kind == 1) {
            c->busWrite32(c, 0x0203f804, p->page);
            c->busWrite32(c, 0x0203f800, 0x56534c47);
            atomic_store(&p->request, 3);
        } else {
            if (c->busRead32(c, 0x0203f004) + 1 != p->seq ||
                c->busRead32(c, 0x0203f008) != p->hash) {
                strcpy(p->response, "{\"error\":\"stale_observation\"}");
                atomic_store(&p->request, 2);
                return;
            }
            for (unsigned i = 0; i < 24; i++)
                c->busWrite8(c, 0x0203f804 + i, p->command[i]);
            c->busWrite32(c, 0x0203f800, 0x5653434d);
            strcpy(p->response, "{\"queued\":true}");
            atomic_store(&p->request, 2);
        }
    } else if (req == 3 && c->busRead32(c, 0x0203f800) == 0) {
        unsigned count = c->busRead32(c, 0x0203f808);
        if (count > 64)
            count = 64;
        char commands[3073];
        hexbytes(c, 0x0203f900, count * 24, commands);
        snprintf(p->response, sizeof(p->response),
                 "{\"count\":%u,\"total\":%u,\"commands\":\"%s\"}", count,
                 c->busRead32(c, 0x0203f80c), commands);
        atomic_store(&p->request, 2);
    }
}
int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    if (argc < 3)
        return 2;
    terrainAddress = strtoul(argv[2], NULL, 16);
    redOpener = argc > 3;
    mLogSetDefaultLogger(&logger);
    struct GBASIOLockstepCoordinator link;
    GBASIOLockstepCoordinatorInit(&link);
    for (int i = 0; i < 2; i++) {
        struct Player *p = &players[i];
        struct mCore *c = mCoreFind(argv[1]);
        if (!c || !c->init(c))
            return 3;
        mCoreInitConfig(c, NULL);
        c->opts.audioSync = false;
        c->opts.videoSync = false;
        c->setVideoBuffer(c, p->pixels, 240);
        if (!mCoreLoadFile(c, argv[1]))
            return 4;
        p->thread.logger.logger = &logger;
        p->thread.core = c;
        p->thread.frameCallback = callback;
        p->thread.userData = p;
        mLockstepThreadUserInit(&p->user, &p->thread);
        GBASIOLockstepDriverCreate(&p->driver, &p->user.d);
        GBASIOLockstepCoordinatorAttach(&link, &p->driver);
        GBASIOSetDriver(&((struct GBA *)c->board)->sio, &p->driver.d);
    }
    for (int i = 0; i < 2; i++)
        if (!mCoreThreadStart(&players[i].thread))
            return 5;
    char input[1024];
    puts("{\"ready\":true}");
    fflush(stdout);
    while (fgets(input, sizeof(input), stdin)) {
        char op[32], hex[49];
        unsigned seat, page = 0, seq = 0, hash = 0;
        if (!strncmp(input, "quit", 4))
            break;
        if (sscanf(input, "%31s %u", op, &seat) != 2 || seat > 1) {
            puts("{\"error\":\"bad_request\"}");
            fflush(stdout);
            continue;
        }
        struct Player *p = &players[seat];
        p->kind = 0;
        if (!strcmp(op, "legal")) {
            sscanf(input, "%*s %*u %u", &page);
            p->kind = 1;
            p->page = page;
        } else if (!strcmp(op, "act")) {
            if (sscanf(input, "%*s %*u %u %u %48s", &seq, &hash, hex) != 3 || strlen(hex) != 48) {
                puts("{\"error\":\"bad_command\"}");
                fflush(stdout);
                continue;
            }
            p->kind = 2;
            p->seq = seq;
            p->hash = hash;
            for (unsigned i = 0; i < 24; i++) {
                unsigned byte;
                sscanf(hex + 2 * i, "%2x", &byte);
                p->command[i] = byte;
            }
        }
        atomic_store(&p->request, 1);
        for (int n = 0; n < 10000 && atomic_load(&p->request) != 2; n++)
            usleep(1000);
        if (atomic_load(&p->request) != 2) {
            puts("{\"error\":\"emulator_timeout\"}");
            fflush(stdout);
            break;
        }
        puts(p->response);
        fflush(stdout);
        atomic_store(&p->request, 0);
    }
    for (int i = 0; i < 2; i++)
        mCoreThreadEnd(&players[i].thread);
    for (int i = 0; i < 2; i++)
        mCoreThreadJoin(&players[i].thread);
    for (int i = 0; i < 2; i++) {
        players[i].user.d.sleep = stopped;
        players[i].user.d.wake = stopped;
    }
    for (int i = 0; i < 2; i++) {
        mCoreConfigDeinit(&players[i].thread.core->config);
        players[i].thread.core->deinit(players[i].thread.core);
        GBASIOLockstepCoordinatorDetach(&link, &players[i].driver);
    }
    GBASIOLockstepCoordinatorDeinit(&link);
    return 0;
}

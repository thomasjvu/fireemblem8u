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
struct Player {
    struct mCoreThread thread;
    struct mLockstepThreadUser user;
    struct GBASIOLockstepDriver driver;
    mColor pixels[240 * 160];
    atomic_uint frame, seq, state, error, seat, active, hash;
    unsigned uiStep;
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
static void callback(struct mCoreThread *t) {
    struct Player *p = t->userData;
    struct mCore *c = t->core;
    unsigned f = c->frameCounter(c), a = 0;
    if (f == 120)
        c->busWrite32(c, 0x0203eff0, 0x56534254);
    if (f >= 200 && f < 203)
        a = 0x80;
    if (f == 220 && redOpener)
        c->busWrite8(c, 0x0203f35b, 1);
    if (f >= 240 && f < 243)
        a = 1;
    c->setKeys(c, a);
    if (f > 300 && (atomic_load(&p->state) != c->busRead8(c, 0x0203f353) ||
                    atomic_load(&p->seq) != c->busRead32(c, 0x0203f004)))
        fprintf(stderr, "transition f%u seat%u state%u seq%u err%u txkind%u lastRx%u progress%u\n",
                f, c->busRead8(c, 0x0203f354), c->busRead8(c, 0x0203f353),
                c->busRead32(c, 0x0203f004), c->busRead8(c, 0x0203f359), c->busRead8(c, 0x0203f363),
                c->busRead32(c, 0x0203f010), c->busRead32(c, 0x0203f014));
    atomic_store(&p->hash, c->busRead32(c, 0x0203f008));
    atomic_store(&p->frame, f);
    atomic_store(&p->seq, c->busRead32(c, 0x0203f004));
    atomic_store(&p->state, c->busRead8(c, 0x0203f353));
    atomic_store(&p->error, c->busRead8(c, 0x0203f359));
    atomic_store(&p->seat, c->busRead8(c, 0x0203f354));
    atomic_store(&p->active, c->busRead8(c, 0x0203f355));
    /* The first transaction comes through native movement and Wait menus,
     * exercising the proposing player's acknowledgement wait in ApplyUnitAction. */
    if (f > 600 && c->busRead32(c, 0x0203f004) == 0 && c->busRead8(c, 0x0203f353) == 1 &&
        c->busRead8(c, 0x0203f354) == c->busRead8(c, 0x0203f355) && p->uiStep < 5) {
        unsigned keys[5] = {1, 0x10, 1, 0x80, 1};
        if (redOpener)
            keys[1] = 0x20;
        c->setKeys(c, (f % 30 < 3) ? keys[p->uiStep] : 0);
        if (f % 30 == 3)
            p->uiStep++;
    }
    /* After a stable linked map, submit alternating EndPhase transactions. */
    if (f > 600 && f % 180 == 0 && c->busRead8(c, 0x0203f353) == 1 &&
        c->busRead8(c, 0x0203f354) == c->busRead8(c, 0x0203f355) &&
        c->busRead32(c, 0x0203f004) < 8 && c->busRead32(c, 0x0203f004) > 0) {
        c->busWrite32(c, 0x0203f804, c->busRead32(c, 0x0203f004) + 1);
        c->busWrite32(c, 0x0203f808, c->busRead32(c, 0x0203f008));
        c->busWrite32(c, 0x0203f80c, c->busRead32(c, 0x03000000));
        c->busWrite32(c, 0x0203f810, c->busRead16(c, 0x03000004));
        unsigned seq = c->busRead32(c, 0x0203f004);
        unsigned actor = 0, target = 0, x = 0, y = 0, kind = 0xf0, item = 0, cost = 0;
        if (seq == 1) {
            actor = redOpener ? 0x81 : 1;
            x = 7;
            y = 3;
            kind = 1;
            cost = 5;
        }
        if (seq == 3) {
            actor = redOpener ? 1 : 0x81;
            target = redOpener ? 0x81 : 1;
            x = redOpener ? 6 : 8;
            y = 3;
            kind = 2;
            cost = 4;
        }
        if (seq == 5) {
            actor = redOpener ? 0x81 : 1;
            x = 7;
            y = 3;
            kind = 0x1a;
            item = 1;
        }
        if (seq == 7)
            kind = 0xf1;
        c->busWrite8(c, 0x0203f812, actor);
        c->busWrite8(c, 0x0203f813, target);
        c->busWrite32(c, 0x0203f814, x | (y << 8) | (kind << 16) | (item << 24));
        c->busWrite32(c, 0x0203f818, cost);
        c->busWrite32(c, 0x0203f800, 0x5653434d);
    }
}
int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    redOpener = argc > 2;
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
    int success = 0;
    for (int j = 0; j < 8000; j++) {
        usleep(10000);
        if (atomic_load(&players[0].frame) > 6000 || atomic_load(&players[1].frame) > 6000)
            break;
        if (atomic_load(&players[0].seq) >= 8 && atomic_load(&players[1].seq) >= 8) {
            success = 1;
            break;
        }
        if (atomic_load(&players[0].error) || atomic_load(&players[1].error))
            break;
    }
    for (int i = 0; i < 2; i++)
        mCoreThreadEnd(&players[i].thread);
    for (int i = 0; i < 2; i++)
        mCoreThreadJoin(&players[i].thread);
    if (atomic_load(&players[0].hash) != atomic_load(&players[1].hash))
        success = 0;
    for (int i = 0; i < 2; i++) {
        char name[100];
        snprintf(name, sizeof(name), "build/versus/linked-seat-%d.ppm", i);
        FILE *f = fopen(name, "wb");
        fprintf(f, "P6\n240 160\n255\n");
        for (int j = 0; j < 240 * 160; j++) {
            unsigned c = players[i].pixels[j];
            fputc(c & 255, f);
            fputc((c >> 8) & 255, f);
            fputc((c >> 16) & 255, f);
        }
        fclose(f);
    }
    for (int i = 0; i < 2; i++)
        printf("seat %d frame=%u seq=%u state=%u error=%u wireSeat=%u active=%u recv0=%04x "
               "recv1=%04x send=%04x hash=%08x cnt=%04x tx=%u rx=%u\n",
               i, atomic_load(&players[i].frame), atomic_load(&players[i].seq),
               atomic_load(&players[i].state), atomic_load(&players[i].error),
               atomic_load(&players[i].seat), atomic_load(&players[i].active),
               players[i].thread.core->busRead16(players[i].thread.core, 0x04000120),
               players[i].thread.core->busRead16(players[i].thread.core, 0x04000122),
               players[i].thread.core->busRead16(players[i].thread.core, 0x0400012a),
               players[i].thread.core->busRead32(players[i].thread.core, 0x0203f008),
               players[i].thread.core->busRead16(players[i].thread.core, 0x04000128),
               players[i].thread.core->busRead8(players[i].thread.core, 0x0203f420),
               players[i].thread.core->busRead8(players[i].thread.core, 0x0203f421));
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 64; j++)
            printf("%02x",
                   players[i].thread.core->busRead8(players[i].thread.core, 0x0203f3a0 + j));
        puts("");
    }
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
    return success ? 0 : 1;
}

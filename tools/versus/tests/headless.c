/* Headless mGBA runner. No campaign save is loaded or written. */
#include <mgba-util/vfs.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/internal/arm/arm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void quiet(struct mLogger *l, int c, enum mLogLevel v, const char *fmt, va_list args) {
    (void)l;
    (void)c;
    (void)v;
    (void)fmt;
    (void)args;
}
static struct mLogger logger = {.log = quiet};
static mColor pixels[240 * 160];
static void shot(const char *p) {
    FILE *f = fopen(p, "wb");
    unsigned i;
    fprintf(f, "P6\n240 160\n255\n");
    for (i = 0; i < 240 * 160; i++) {
        uint32_t c = pixels[i];
        fputc(c & 255, f);
        fputc((c >> 8) & 255, f);
        fputc((c >> 16) & 255, f);
    }
    fclose(f);
}
int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    mLogSetDefaultLogger(&logger);
    struct mCore *c = mCoreFind(argv[1]);
    if (!c || !c->init(c))
        return 3;
    mCoreInitConfig(c, NULL);
    c->setVideoBuffer(c, pixels, 240);
    if (!mCoreLoadFile(c, argv[1]))
        return 4;
    c->reset(c);
    char line[512], name[256];
    unsigned a, b, i;
    while (fgets(line, sizeof(line), stdin)) {
        if (sscanf(line, "frames %u", &a) == 1) {
            for (i = 0; i < a; i++)
                c->runFrame(c);
            printf("frame %u pc %08x cpsr %08x\n", c->frameCounter(c),
                   ((struct ARMCore *)c->cpu)->gprs[15], ((struct ARMCore *)c->cpu)->cpsr.packed);
        } else if (sscanf(line, "keys %x", &a) == 1)
            c->setKeys(c, a);
        else if (sscanf(line, "w32 %x %x", &a, &b) == 2)
            c->busWrite32(c, a, b);
        else if (sscanf(line, "w8 %x %x", &a, &b) == 2)
            c->busWrite8(c, a, b);
        else if (sscanf(line, "r32 %x", &a) == 1)
            printf("%08x %08x\n", a, c->busRead32(c, a));
        else if (sscanf(line, "r8 %x", &a) == 1)
            printf("%08x %02x\n", a, c->busRead8(c, a));
        else if (sscanf(line, "shot %255s", name) == 1)
            shot(name);
        else if (!strncmp(line, "quit", 4))
            break;
        fflush(stdout);
    }
    mCoreConfigDeinit(&c->config);
    c->deinit(c);
    return 0;
}

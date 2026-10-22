/* One real mGBA core per WebAssembly instance. ROM data stays local. */
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static struct mCore *core;
static mColor pixels[240 * 160];
static int16_t samples[4096 * 2];
static void *savedata;
static void quiet(struct mLogger *l,int cat,enum mLogLevel level,const char *fmt,va_list args) { (void)l;(void)cat;(void)level;(void)fmt;(void)args; }
static struct mLogger logger={.log=quiet};
int vs_open(const void *data, unsigned size) {
    free(savedata); savedata = NULL;
    if(core) { mCoreConfigDeinit(&core->config);core->deinit(core);core=NULL; }
    mLogSetDefaultLogger(&logger);
    struct VFile *file=VFileMemChunk(data,size);
    if(!file) return 0;
    core=mCoreFindVF(file);
    if(!core || !core->init(core)) { file->close(file); return 0; }
    mCoreInitConfig(core,NULL);
    core->setVideoBuffer(core,pixels,240);
    core->setAudioBufferSize(core,4096);
    if(!core->loadROM(core,file)) { file->close(file); return 0; }
    /* Attach an in-memory SRAM file before boot so imported battery data is
     * retained even before FE8 first probes its save hardware. */
    unsigned char *blank = malloc(32768);
    if (!blank) return 0;
    memset(blank, 0xFF, 32768);
    struct VFile *save = VFileMemChunk(blank, 32768);
    free(blank);
    if (!save || !core->loadSave(core, save)) { if (save) save->close(save); return 0; }
    core->reset(core); return 1;
}
void vs_frame(void) { core->runFrame(core); }
void vs_keys(unsigned keys) { core->setKeys(core,keys&1023); }
void *vs_pixels(void) { return pixels; }
unsigned vs_read(unsigned address) { return core->busRead8(core,address); }
void vs_write(unsigned address,unsigned value) { core->busWrite8(core,address,value); }
unsigned vs_audio(void) { return mAudioBufferRead(core->getAudioBuffer(core),samples,4096); }
void *vs_audio_ptr(void) { return samples; }
unsigned vs_sample_rate(void) { return core->audioSampleRate(core); }
unsigned vs_savedata(void) {
    free(savedata); savedata = NULL;
    return core->savedataClone(core, &savedata);
}
void *vs_savedata_ptr(void) { return savedata; }
int vs_restore_savedata(const void *data, unsigned size) {
    if (size != 32768) return 0; /* FE8 USA uses 256-kbit SRAM. */
    return core->savedataRestore(core, data, size, true);
}
void vs_reset(void) { core->setKeys(core, 0); core->reset(core); }
unsigned vs_state_size(void) { return core->stateSize(core); }
int vs_save_state(void *state) { return core->saveState(core, state); }
int vs_load_state(const void *state, unsigned size) {
    if (size != core->stateSize(core)) return 0;
    bool loaded = core->loadState(core, state);
    core->setKeys(core, 0);
    return loaded;
}

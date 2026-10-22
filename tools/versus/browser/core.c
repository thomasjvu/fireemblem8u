/* One real mGBA core per WebAssembly instance. ROM data stays local. */
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <stdint.h>
static struct mCore *core;
static mColor pixels[240 * 160];
static int16_t samples[4096 * 2];
static void quiet(struct mLogger *l,int cat,enum mLogLevel level,const char *fmt,va_list args) { (void)l;(void)cat;(void)level;(void)fmt;(void)args; }
static struct mLogger logger={.log=quiet};
int vs_open(const void *data, unsigned size) {
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

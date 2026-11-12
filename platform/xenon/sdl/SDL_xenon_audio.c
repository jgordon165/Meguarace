/* SDL audio for the Xbox 360: 48 kHz 16-bit stereo, little-endian, fed from
 * the game's own thread (there are no threads): XENON_AudioPump() runs the
 * callback whenever the console's sound buffer has room, from the video
 * driver's event pump and present. Takes the place of SDL's dummy audio
 * driver (DUMMYAUDIO_bootstrap); modelled on SDL's Emscripten driver, which
 * also has no audio thread. */
#include "SDL_internal.h"

#include "SDL_audio.h"
#include "audio/SDL_audio_c.h"
#include "audio/SDL_sysaudio.h"

#include "xenon_platform.h"

static SDL_AudioDevice *xenon_device;

/* Keep about 0.15 s queued: enough to ride out a slow frame. */
#define TARGET_QUEUED (XP_AUDIO_RATE * 4 * 15 / 100)
#define BUFFER_BYTES 65536

static void fill_one(SDL_AudioDevice *this)
{
    SDL_AudioCallback callback = this->callbackspec.callback;
    const int stream_len = this->callbackspec.size;

    if (!SDL_AtomicGet(&this->enabled) || SDL_AtomicGet(&this->paused)) {
        if (this->stream) SDL_AudioStreamClear(this->stream);
        SDL_memset(this->work_buffer, this->spec.silence, this->spec.size);
    } else if (!this->stream) {
        SDL_LockMutex(this->mixer_lock);
        callback(this->callbackspec.userdata, this->work_buffer, stream_len);
        SDL_UnlockMutex(this->mixer_lock);
    } else {
        int got;
        while (SDL_AudioStreamAvailable(this->stream) < (int)this->spec.size) {
            SDL_LockMutex(this->mixer_lock);
            callback(this->callbackspec.userdata, this->work_buffer, stream_len);
            SDL_UnlockMutex(this->mixer_lock);
            if (SDL_AudioStreamPut(this->stream, this->work_buffer, stream_len) == -1) {
                SDL_AudioStreamClear(this->stream);
                SDL_AtomicSet(&this->enabled, 0);
                break;
            }
        }
        got = SDL_AudioStreamGet(this->stream, this->work_buffer, this->spec.size);
        if (got != (int)this->spec.size) SDL_memset(this->work_buffer, this->spec.silence, this->spec.size);
    }
    xp_audio_submit(this->work_buffer, (int)this->spec.size);
}

void XENON_AudioPump(void)
{
    SDL_AudioDevice *this = xenon_device;
    int guard = 64;
    if (!this) return;
    while (guard-- > 0) {
        int free_bytes = xp_audio_free();
        int queued = BUFFER_BYTES - free_bytes;
        if (queued >= TARGET_QUEUED || free_bytes < (int)this->spec.size) break;
        fill_one(this);
    }
}

static int XENON_OpenDevice(_THIS, const char *devname)
{
    (void)devname;
    this->spec.freq = XP_AUDIO_RATE;
    this->spec.format = AUDIO_S16LSB;
    this->spec.channels = 2;
    if (this->spec.samples == 0 || this->spec.samples > 4096) this->spec.samples = 1024;
    SDL_CalculateAudioSpec(&this->spec);
    xp_audio_init();
    xenon_device = this;
    return 0;
}

static void XENON_CloseDevice(_THIS)
{
    if (xenon_device == this) xenon_device = NULL;
}

static SDL_bool XENON_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = XENON_OpenDevice;
    impl->CloseDevice = XENON_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->ProvidesOwnCallbackThread = SDL_TRUE;
    return SDL_TRUE;
}

AudioBootStrap DUMMYAUDIO_bootstrap = {
    "xenon", "Xbox 360 audio", XENON_Init, SDL_FALSE
};

/* Headless libretro host for the Saturn tests (tests/saturn/run_emulator_test.sh).
 *
 * Runs a core (Yabause, with its HLE BIOS: no Sega BIOS is needed or used)
 * on a disc image for N frames, saves screenshots (PPM) at chosen frames and
 * all audio to a WAV, and presses Saturn pad buttons on a script.
 *
 *   lrhost core.so game.cue frames out_prefix [shot@F ...] [A@F[:len] ...] [cart=4M_ram]
 *   buttons: A B C X Y Z S(tart) U D < >  (the Saturn pad's, mapped as the
 *   Yabause core maps a RetroPad)
 */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libretro.h>

static unsigned fw, fh; static size_t fpitch;
static uint16_t* frame;
static enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_0RGB1555;
static FILE* wav; static uint32_t wav_frames;
static const char* cart = "none";
static int cur_frame;
struct press { int id, at, len; };
static struct press presses[64]; static int npress;

static void log_cb(enum retro_log_level l, const char* f, ...) {
    va_list a; va_start(a, f);
    if (l >= RETRO_LOG_WARN) vfprintf(stderr, f, a);
    va_end(a);
}

static bool env(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)data)->log = log_cb; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: fmt = *(enum retro_pixel_format*)data; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data = "/tmp/lrsys"; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable* v = data;
        if (!strcmp(v->key, "yabause_force_hle_bios")) { v->value = "enabled"; return true; }
        if (!strcmp(v->key, "yabause_frameskip")) { v->value = "disabled"; return true; }
        if (!strcmp(v->key, "yabause_addon_cartridge")) { v->value = cart; return true; }
        if (!strcmp(v->key, "yabause_numthreads")) { v->value = "1"; return true; }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data = true; return true;
    default: return false;
    }
}

static void video(const void* d, unsigned w, unsigned h, size_t pitch) {
    if (!d) return;
    if (!frame || w != fw || h != fh) { free(frame); frame = malloc(w * h * 2); fw = w; fh = h; }
    for (unsigned y = 0; y < h; y++) memcpy(frame + y * w, (const uint8_t*)d + y * pitch, w * 2);
    fpitch = pitch;
}

static size_t audio_batch(const int16_t* d, size_t n) { fwrite(d, 4, n, wav); wav_frames += n; return n; }
static void audio_one(int16_t l, int16_t r) { int16_t s[2] = {l, r}; audio_batch(s, 1); }
static void poll(void) {}
static int16_t input(unsigned port, unsigned dev, unsigned idx, unsigned id) {
    if (port != 0 || dev != RETRO_DEVICE_JOYPAD) return 0;
    for (int i = 0; i < npress; i++)
        if (presses[i].id == (int)id && cur_frame >= presses[i].at && cur_frame < presses[i].at + presses[i].len) return 1;
    return 0;
}

static void shot(const char* path) {
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6 %u %u 255\n", fw, fh);
    for (unsigned i = 0; i < fw * fh; i++) {
        uint16_t p = frame[i]; uint8_t c[3];
        if (fmt == RETRO_PIXEL_FORMAT_RGB565) { c[0] = (p >> 11) << 3; c[1] = ((p >> 5) & 63) << 2; c[2] = (p & 31) << 3; }
        else { c[0] = ((p >> 10) & 31) << 3; c[1] = ((p >> 5) & 31) << 3; c[2] = (p & 31) << 3; }
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

static void wav_header(FILE* f, uint32_t frames, unsigned rate) {
    uint32_t data = frames * 4;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); uint32_t v = 36 + data; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    uint16_t s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    v = rate; fwrite(&v, 4, 1, f); v = rate * 4; fwrite(&v, 4, 1, f);
    s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage\n"); return 2; }
    void* h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
#define SYM(n) __typeof__(&n) p_##n = dlsym(h, #n)
    SYM(retro_set_environment); SYM(retro_set_video_refresh); SYM(retro_set_audio_sample);
    SYM(retro_set_audio_sample_batch); SYM(retro_set_input_poll); SYM(retro_set_input_state);
    SYM(retro_init); SYM(retro_load_game); SYM(retro_set_controller_port_device); SYM(retro_run); SYM(retro_get_system_av_info);
    int frames = atoi(argv[3]);
    const char* out = argv[4];
    int shots[256]; int nshots = 0;
    for (int i = 5; i < argc; i++) {
        if (!strncmp(argv[i], "cart=", 5)) { cart = argv[i] + 5; continue; }
        if (!strncmp(argv[i], "shot@", 5)) { shots[nshots++] = atoi(argv[i] + 5); continue; }
        int id = -1;
        switch (argv[i][0]) {
        case 'A': id = RETRO_DEVICE_ID_JOYPAD_B; break;      /* yabause: retro B = Saturn A */
        case 'B': id = RETRO_DEVICE_ID_JOYPAD_A; break;
        case 'C': id = RETRO_DEVICE_ID_JOYPAD_R; break;
        case 'X': id = RETRO_DEVICE_ID_JOYPAD_Y; break;
        case 'Y': id = RETRO_DEVICE_ID_JOYPAD_X; break;
        case 'Z': id = RETRO_DEVICE_ID_JOYPAD_L; break;
        case 'S': id = RETRO_DEVICE_ID_JOYPAD_START; break;
        case 'U': id = RETRO_DEVICE_ID_JOYPAD_UP; break;
        case 'D': id = RETRO_DEVICE_ID_JOYPAD_DOWN; break;
        case '<': id = RETRO_DEVICE_ID_JOYPAD_LEFT; break;
        case '>': id = RETRO_DEVICE_ID_JOYPAD_RIGHT; break;
        }
        const char* at = strchr(argv[i], '@');
        if (id < 0 || !at) { fprintf(stderr, "bad arg %s\n", argv[i]); return 2; }
        presses[npress].id = id;
        presses[npress].at = atoi(at + 1);
        const char* colon = strchr(at, ':');
        presses[npress].len = colon ? atoi(colon + 1) : 6;
        npress++;
    }
    char path[512];
    snprintf(path, sizeof path, "%s.wav", out);
    wav = fopen(path, "wb");
    wav_header(wav, 0, 44100);
    p_retro_set_environment(env);
    p_retro_set_video_refresh(video);
    p_retro_set_audio_sample(audio_one);
    p_retro_set_audio_sample_batch(audio_batch);
    p_retro_set_input_poll(poll);
    p_retro_set_input_state(input);
    p_retro_init();
    struct retro_game_info gi = {argv[2], NULL, 0, NULL};
    if (!p_retro_load_game(&gi)) { fprintf(stderr, "load failed\n"); return 1; }
    p_retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    struct retro_system_av_info av; p_retro_get_system_av_info(&av);
    for (cur_frame = 0; cur_frame < frames; cur_frame++) {
        p_retro_run();
        for (int s = 0; s < nshots; s++)
            if (shots[s] == cur_frame && frame) {
                snprintf(path, sizeof path, "%s_%05d.ppm", out, cur_frame);
                shot(path);
            }
    }
    wav_header(wav, wav_frames, (unsigned)av.timing.sample_rate);
    fclose(wav);
    fprintf(stderr, "ran %d frames, %ux%u, %u audio frames at %.0f Hz\n", frames, fw, fh, wav_frames, av.timing.sample_rate);
    return 0;
}

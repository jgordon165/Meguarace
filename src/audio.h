#pragma once
// Small software mixer: the game's FM music (HERAD songs on an emulated
// OPL2, src/fm.h), an optional recorded-music track (WAV), the video
// soundtrack stream, and a handful of PCM voices for sound effects decoded
// from the game's data files.

#include <SDL.h>
#include "fm.h"
#include "resampler.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Audio {
public:
    bool init();
    void shutdown();

    // FM music: an unpacked HERAD song from MEGARACE.DAT. Replaces any
    // song already playing. stopAll() leaves it alone.
    bool playSong(const uint8_t* data, size_t len, bool loop);
    void stopSong();
    void fadeSong(double seconds);   // fades out, then stops (the driver's fade)
    bool songPlaying();
    void setSongVolume(float v);

    // Recorded music (optional --music folder): WAV, converted to the device format.
    bool playMusicWav(const std::string& hostPath, bool loop);
    void stopMusic();
    bool musicPlaying();
    void setMusicVolume(float v);

    // Raw PCM as found in DOS game data (8-bit unsigned or 16-bit signed LE).
    int  playPcm(const uint8_t* data, size_t len, int rate, int bits, int channels,
                 float volume, bool loop);
    void stopVoice(int id);
    void stopAll();

    // Streamed PCM (video soundtracks): push per-frame audio as it decodes.
    void streamReset();
    void streamPush(const uint8_t* data, size_t len, int rate, int bits, int channels);
    double streamQueuedSeconds();
    uint64_t streamPlayedFrames();   // sample frames played since reset (A/V clock)
    int deviceRate() const { return spec_.freq; }
    bool available() const { return dev_ != 0; }

private:
    struct Source {
        std::vector<int16_t> samples;   // interleaved stereo at device rate
        size_t pos   = 0;
        float  vol   = 1.0f;
        bool   loop  = false;
        bool   active = false;
        int    id    = 0;
    };

    static void callback(void* user, Uint8* stream, int len);
    void mix(int16_t* out, int frames);
    bool convertToDevice(const uint8_t* data, size_t len, SDL_AudioFormat fmt,
                         int channels, int rate, std::vector<int16_t>& out);   // WAV files

    SDL_AudioDeviceID dev_ = 0;
    SDL_AudioSpec spec_{};
    std::mutex mtx_;
    Source music_;
    std::unique_ptr<FmMusic> song_;
    float songVol_ = 0.6f;
    int songFade_ = 0, songFadeTotal_ = 0;   // frames left / length of a fade-out
    Source stream_;
    Resampler streamResampler_;     // persists across pushes (exact A/V timing)
    uint64_t streamPlayed_ = 0;
    std::vector<Source> voices_ = std::vector<Source>(16);
    int nextId_ = 1;
};

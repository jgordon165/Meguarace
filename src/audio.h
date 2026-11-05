#pragma once
// Small software mixer: one streamed music source (CD audio tracks ripped
// from the user's own disc) plus a handful of PCM voices for sound effects
// decoded from the game's data files.

#include <SDL.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class Audio {
public:
    bool init();
    void shutdown();

    // Music: 16-bit stereo samples already converted to the device format.
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
                         int channels, int rate, std::vector<int16_t>& out);

    SDL_AudioDeviceID dev_ = 0;
    SDL_AudioSpec spec_{};
    std::mutex mtx_;
    Source music_;
    Source stream_;
    uint64_t streamPlayed_ = 0;
    std::vector<Source> voices_ = std::vector<Source>(16);
    int nextId_ = 1;
};

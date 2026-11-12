#pragma once
// Small software mixer: the game's FM music (HERAD songs on an emulated
// OPL2, src/fm.h), an optional recorded-music track (WAV), the video
// soundtrack stream, and a handful of PCM voices for sound effects decoded
// from the game's data files.

#include <SDL.h>
#include "fm.h"
#include "resampler.h"
#include "voice_filter.h"
#include <cstdint>
#include <memory>
#include "compat_mutex.h"
#include <string>
#include <vector>

class Audio {
public:
    bool init();
    void shutdown();

    // FM music: an unpacked HERAD song from MEGARACE.DAT. Replaces any
    // song already playing. stopAll() leaves it alone. `gm`: the SoundFont
    // style's instrument overrides (FmMusic::load).
    bool playSong(const uint8_t* data, size_t len, bool loop, const std::vector<int>& gm = {});
    // The music style (fm.h). Switching carries on the song that's playing
    // from where it is. SoundFont loads the first .sf2 found in the folders
    // given to setSoundFontDirs (once); so does Enhanced (a remix on the
    // same SoundFont). Without one they play Stereo.
    void setMusicStyle(MusicStyle s);
    MusicStyle musicStyle() const { return style_; }
    void setSoundFontDirs(std::vector<std::string> dirs) { sfDirs_ = std::move(dirs); }
    std::string soundFontPath();     // the .sf2 the SoundFont style uses, or ""
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
    // Hiss reduction for the stream (the announcer's clips): on until the
    // next streamReset(). `noise` from VoiceFilter::profile(). 8-bit mono only.
    void streamVoiceFilter(std::vector<float> noise, int rate);
    void streamVoiceBypass(bool bypass);   // original sound, same timing
    void streamEnd();                      // no more pushes: let the filter's last samples out
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
    compat::Mutex mtx_;
    Source music_;
    std::unique_ptr<FmMusic> song_;
    MusicStyle style_ = MusicStyle::Original;
    std::shared_ptr<SoundFontBank> bank_;
    bool bankTried_ = false;
    std::vector<std::string> sfDirs_;
    std::vector<uint8_t> songBytes_;     // the song playing, to switch styles
    bool songLoop_ = true;
    std::vector<int> songGm_;
    std::shared_ptr<SoundFontBank> bank();
    float songVol_ = 0.6f;
    int songFade_ = 0, songFadeTotal_ = 0;   // frames left / length of a fade-out
    Source stream_;
    Resampler streamResampler_;     // persists across pushes (exact A/V timing)
    VoiceFilter streamFilter_;
    int streamRate_ = 11111;
    void pushFiltered(const std::vector<float>& out);
    void queueStream(const std::vector<int16_t>& pcm);
    uint64_t streamPlayed_ = 0;
    std::vector<Source> voices_ = std::vector<Source>(16);
    int nextId_ = 1;
};

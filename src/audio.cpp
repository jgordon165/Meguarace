#include "audio.h"

#include <cmath>

#include <algorithm>
#include <cstdio>
#include <cstring>

bool Audio::init() {
    if (!SDL_WasInit(SDL_INIT_AUDIO)) return false;
    SDL_AudioSpec want{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &Audio::callback;
    want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &spec_, 0);
    if (!dev_) {
        std::fprintf(stderr, "audio disabled: %s\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(dev_, 0);
    return true;
}

void Audio::shutdown() {
    if (dev_) SDL_CloseAudioDevice(dev_);
    dev_ = 0;
}

bool Audio::convertToDevice(const uint8_t* data, size_t len, SDL_AudioFormat fmt,
                            int channels, int rate, std::vector<int16_t>& out) {
    if (!dev_) return false;
    SDL_AudioStream* st = SDL_NewAudioStream(fmt, static_cast<Uint8>(channels), rate,
                                             AUDIO_S16SYS, 2, spec_.freq);
    if (!st) return false;
    SDL_AudioStreamPut(st, data, static_cast<int>(len));
    SDL_AudioStreamFlush(st);
    int avail = SDL_AudioStreamAvailable(st);
    out.resize(static_cast<size_t>(avail) / sizeof(int16_t));
    SDL_AudioStreamGet(st, out.data(), avail);
    SDL_FreeAudioStream(st);
    return true;
}

bool Audio::playMusicWav(const std::string& hostPath, bool loop) {
    if (!dev_) return false;
    SDL_AudioSpec wspec{};
    Uint8* buf = nullptr;
    Uint32 blen = 0;
    if (!SDL_LoadWAV(hostPath.c_str(), &wspec, &buf, &blen)) return false;
    std::vector<int16_t> pcm;
    bool ok = convertToDevice(buf, blen, wspec.format, wspec.channels, wspec.freq, pcm);
    SDL_FreeWAV(buf);
    if (!ok) return false;
    std::lock_guard<std::mutex> lk(mtx_);
    float vol = music_.active ? music_.vol : 1.0f;
    music_ = Source{std::move(pcm), 0, vol, loop, true, 0};
    return true;
}

bool Audio::playSong(const uint8_t* data, size_t len, bool loop) {
    if (!dev_) return false;
    auto m = std::make_unique<FmMusic>();
    if (!m->load(data, len, loop)) return false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        song_.swap(m);
        songFade_ = songFadeTotal_ = 0;
    }
    return true;                       // the old song is freed here, outside the lock
}

void Audio::stopSong() {
    std::unique_ptr<FmMusic> old;
    std::lock_guard<std::mutex> lk(mtx_);
    old = std::move(song_);
}
void Audio::fadeSong(double seconds) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!song_) return;
    songFadeTotal_ = std::max(1, int(seconds * spec_.freq));
    songFade_ = songFadeTotal_;
}
bool Audio::songPlaying() { std::lock_guard<std::mutex> lk(mtx_); return song_ && song_->playing(); }
void Audio::setSongVolume(float v) { std::lock_guard<std::mutex> lk(mtx_); songVol_ = v; }

void Audio::stopMusic() { std::lock_guard<std::mutex> lk(mtx_); music_.active = false; }
bool Audio::musicPlaying() { std::lock_guard<std::mutex> lk(mtx_); return music_.active; }
void Audio::setMusicVolume(float v) { std::lock_guard<std::mutex> lk(mtx_); music_.vol = v; }

int Audio::playPcm(const uint8_t* data, size_t len, int rate, int bits, int channels,
                   float volume, bool loop) {
    if (!dev_) return 0;
    std::vector<int16_t> pcm;
    Resampler rs;
    rs.push(data, len, bits, channels, rate, spec_.freq, pcm);
    rs.finish(pcm);
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = std::find_if(voices_.begin(), voices_.end(), [](const Source& s) { return !s.active; });
    if (it == voices_.end()) it = voices_.begin();   // steal the oldest slot
    *it = Source{std::move(pcm), 0, volume, loop, true, nextId_++};
    return it->id;
}

void Audio::stopVoice(int id) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& v : voices_) if (v.id == id) v.active = false;
}

void Audio::stopAll() {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& v : voices_) v.active = false;
    music_.active = false;
}

void Audio::streamReset() {
    std::lock_guard<std::mutex> lk(mtx_);
    stream_ = Source{};
    stream_.active = true;
    streamPlayed_ = 0;
    streamResampler_.reset();
    streamFilter_.stop();
    streamFilter_.setBypass(false);
}

void Audio::streamVoiceFilter(std::vector<float> noise, int rate) {
    streamFilter_.start(std::move(noise), rate);
}

void Audio::streamVoiceBypass(bool bypass) { streamFilter_.setBypass(bypass); }

void Audio::streamEnd() {
    if (!dev_ || !streamFilter_.active()) return;
    std::vector<float> tail;
    streamFilter_.flush(tail);
    pushFiltered(tail);
}

void Audio::streamPush(const uint8_t* data, size_t len, int rate, int bits, int channels) {
    if (!dev_ || len == 0 || rate <= 0) return;
    std::vector<int16_t> pcm;
    // One resampler for the whole stream: per-chunk conversion loses samples
    // at every chunk boundary and makes the sound run ahead of the video.
    if (streamFilter_.active() && bits == 8 && channels == 1) {
        std::vector<float> in(len), out;
        for (size_t i = 0; i < len; ++i) in[i] = (float(data[i]) - 128.0f) / 128.0f;
        streamFilter_.process(in.data(), in.size(), out);
        streamRate_ = rate;
        pushFiltered(out);
        return;
    }
    streamResampler_.push(data, len, bits, channels, rate, spec_.freq, pcm);
    queueStream(pcm);
}

// Filtered samples (-1..1, mono, at the stream's rate) into the stream.
void Audio::pushFiltered(const std::vector<float>& out) {
    if (out.empty()) return;
    std::vector<uint8_t> le(out.size() * 2);
    for (size_t i = 0; i < out.size(); ++i) {
        auto v = int16_t(std::lround(std::clamp(out[i], -1.0f, 32767.0f / 32768.0f) * 32768.0f));
        le[i * 2] = uint8_t(uint16_t(v) & 0xFF);
        le[i * 2 + 1] = uint8_t(uint16_t(v) >> 8);
    }
    std::vector<int16_t> pcm;
    streamResampler_.push(le.data(), le.size(), 16, 1, streamRate_, spec_.freq, pcm);
    queueStream(pcm);
}

void Audio::queueStream(const std::vector<int16_t>& pcm) {
    std::lock_guard<std::mutex> lk(mtx_);
    // Drop what's already played so the buffer doesn't grow forever.
    if (stream_.pos > 0) {
        stream_.samples.erase(stream_.samples.begin(),
                              stream_.samples.begin() + static_cast<long>(stream_.pos));
        stream_.pos = 0;
    }
    stream_.samples.insert(stream_.samples.end(), pcm.begin(), pcm.end());
    stream_.active = true;
}

double Audio::streamQueuedSeconds() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!dev_) return 0;
    return double(stream_.samples.size() - stream_.pos) / 2.0 / spec_.freq;
}

uint64_t Audio::streamPlayedFrames() {
    std::lock_guard<std::mutex> lk(mtx_);
    return streamPlayed_;
}

void Audio::callback(void* user, Uint8* stream, int len) {
    auto* self = static_cast<Audio*>(user);
    std::memset(stream, 0, static_cast<size_t>(len));
    self->mix(reinterpret_cast<int16_t*>(stream), len / 4);
}

void Audio::mix(int16_t* out, int frames) {
    std::lock_guard<std::mutex> lk(mtx_);
    const size_t n = static_cast<size_t>(frames) * 2;
    std::vector<int32_t> acc(n, 0);
    auto add = [&](Source& s) {
        if (!s.active || s.samples.empty()) return;
        for (size_t i = 0; i < n; ++i) {
            if (s.pos >= s.samples.size()) {
                if (!s.loop) { s.active = false; return; }
                s.pos = 0;
            }
            acc[i] += static_cast<int32_t>(s.samples[s.pos++] * s.vol);
        }
    };
    if (song_) {
        float g = songVol_;
        if (songFadeTotal_) {
            g *= float(songFade_) / float(songFadeTotal_);
            songFade_ = std::max(0, songFade_ - frames);
        }
        song_->render(acc.data(), frames, spec_.freq, g);
        if (songFadeTotal_ && songFade_ == 0) { song_.reset(); songFadeTotal_ = 0; }
    }
    add(music_);
    for (auto& v : voices_) add(v);
    // The stream never loops or deactivates on underrun; it just waits.
    for (size_t i = 0; i < n && stream_.pos < stream_.samples.size(); ++i) {
        acc[i] += stream_.samples[stream_.pos++];
        if (i & 1) ++streamPlayed_;
    }
    for (size_t i = 0; i < n; ++i)
        out[i] = static_cast<int16_t>(std::clamp(acc[i], -32768, 32767));
}

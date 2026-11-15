#pragma once
// The audio mixer's lock. On the Xbox 360 there are no threads in the C++
// library: with xsdk the sound is mixed on a thread of the platform's own
// (xp_audio_start_feeder), so the lock is the platform's; with LibXenon it is
// mixed on the game's own thread and xp_lock does nothing.

#ifndef XENON
#include <mutex>
namespace compat {
using Mutex = std::mutex;
using Lock = std::lock_guard<std::mutex>;
}
#else
extern "C" {
void xp_lock(void);
void xp_unlock(void);
}
namespace compat {
struct Mutex {
    void lock() { xp_lock(); }
    void unlock() { xp_unlock(); }
};
struct Lock {
    explicit Lock(Mutex& m) : m_(m) { m_.lock(); }
    ~Lock() { m_.unlock(); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Mutex& m_;
};
}
#endif

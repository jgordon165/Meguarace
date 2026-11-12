#pragma once
// The audio mixer's lock. On the Xbox 360 (LibXenon) there are no threads in
// the C++ library and the sound is mixed on the game's own thread, so the
// lock does nothing there.

#ifndef XENON
#include <mutex>
namespace compat {
using Mutex = std::mutex;
using Lock = std::lock_guard<std::mutex>;
}
#else
namespace compat {
struct Mutex {
    void lock() {}
    void unlock() {}
};
struct Lock {
    explicit Lock(Mutex&) {}
};
}
#endif

// Minimal platform contract. The core has no OS dependencies: everything
// that touches the outside world goes through these functions and through
// net::Connection.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace tgnx::platform {

void secureRandom(void* out, size_t size);

// Threading primitives that work on both the host and libnx (which has no
// pthreads, so the standard C++ thread library is unavailable on Switch).
class Thread {
public:
    Thread() = default;
    ~Thread();
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    void start(std::function<void()> fn);
    void join();
    struct Impl;

private:
    Impl* impl_ = nullptr;
};

class Mutex {
public:
    Mutex();
    ~Mutex();
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    void lock();
    void unlock();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

class Lock {
public:
    explicit Lock(Mutex& m) : m_(m) { m_.lock(); }
    ~Lock() { m_.unlock(); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    Mutex& m_;
};

void sleepMs(int ms);
bool initNetwork();
void shutdownNetwork();
int64_t unixTimeMs();
uint64_t monotonicMs();
void logLine(const std::string& line);

// Persistence for config/session data. Paths are platform-specific.
bool readFile(const std::string& path, std::string* out);
bool writeFile(const std::string& path, const std::string& data);

// Root for config files (e.g. "sdmc:/switch/telegramnx" on Switch,
// "~/.config/telegramnx" on the host).
std::string configDir();

}  // namespace tgnx::platform

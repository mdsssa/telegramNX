#include "platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <mutex>
#include <thread>
#include <ctime>
#include <random>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace tgnx::platform {

struct Thread::Impl {
    std::thread thread;
    std::function<void()> fn;
};

Thread::~Thread() { join(); }

void Thread::start(std::function<void()> fn) {
    if (impl_) return;
    impl_ = new Impl();
    impl_->fn = std::move(fn);
    impl_->thread = std::thread([this]() { impl_->fn(); });
}

void Thread::join() {
    if (!impl_) return;
    if (impl_->thread.joinable()) impl_->thread.join();
    delete impl_;
    impl_ = nullptr;
}

struct Mutex::Impl {
    std::mutex mutex;
};

Mutex::Mutex() : impl_(new Impl()) {}
Mutex::~Mutex() { delete impl_; }
void Mutex::lock() { impl_->mutex.lock(); }
void Mutex::unlock() { impl_->mutex.unlock(); }

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void secureRandom(void* out, size_t size) {
    static std::random_device rd;
    auto* p = static_cast<uint8_t*>(out);
    for (size_t i = 0; i < size; ++i) p[i] = uint8_t(rd() & 0xFF);
}

bool initNetwork() { return true; }
void shutdownNetwork() {}

int64_t unixTimeMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

uint64_t monotonicMs() {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000 + uint64_t(ts.tv_nsec) / 1000000;
}

void logLine(const std::string& line) {
    std::fprintf(stderr, "%s\n", line.c_str());
    std::fflush(stderr);
}

bool readFile(const std::string& path, std::string* out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out->clear();
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out->append(buf, n);
    std::fclose(f);
    return true;
}

bool writeFile(const std::string& path, const std::string& data) {
    // Create parent dirs for a simple one-level path like .../dir/file.
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        std::string dir = path.substr(0, slash);
        std::string cur;
        for (size_t i = 0; i < dir.size(); ++i) {
            cur.push_back(dir[i]);
            if (dir[i] == '/' && cur.size() > 1) mkdir(cur.c_str(), 0755);
        }
        mkdir(dir.c_str(), 0755);
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

std::string configDir() {
    const char* home = std::getenv("HOME");
    std::string base = home ? home : ".";
    std::string dir = base + "/.config/telegramnx";
    mkdir((base + "/.config").c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    return dir;
}

}  // namespace tgnx::platform

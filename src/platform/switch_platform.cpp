#include "platform/platform.h"

#include <cstdio>
#include <ctime>
#include <cstring>
#include <switch.h>
#include <switch/arm/counter.h>
#include <sys/stat.h>

namespace tgnx::platform {

struct Thread::Impl {
    ::Thread thread;
    std::function<void()> fn;
};

namespace {
void threadTrampoline(void* arg) {
    auto* impl = static_cast<Thread::Impl*>(arg);
    impl->fn();
}
}  // namespace

Thread::~Thread() { join(); }

void Thread::start(std::function<void()> fn) {
    if (impl_) return;
    impl_ = new Impl();
    impl_->fn = std::move(fn);
    if (R_FAILED(threadCreate(&impl_->thread, threadTrampoline, impl_, nullptr, 0x80000, 0x2C, -2))) {
        delete impl_;
        impl_ = nullptr;
        return;
    }
    threadStart(&impl_->thread);
}

void Thread::join() {
    if (!impl_) return;
    threadWaitForExit(&impl_->thread);
    threadClose(&impl_->thread);
    delete impl_;
    impl_ = nullptr;
}

struct Mutex::Impl {
    ::Mutex mutex;
};

Mutex::Mutex() : impl_(new Impl()) { mutexInit(&impl_->mutex); }
Mutex::~Mutex() { delete impl_; }
void Mutex::lock() { mutexLock(&impl_->mutex); }
void Mutex::unlock() { mutexUnlock(&impl_->mutex); }

void sleepMs(int ms) { svcSleepThread(int64_t(ms) * 1000000); }

void secureRandom(void* out, size_t size) { randomGet(out, size); }

bool initNetwork() { return R_SUCCEEDED(socketInitializeDefault()); }
void shutdownNetwork() { socketExit(); }

int64_t unixTimeMs() {
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    return int64_t(ts.tv_sec) * 1000 + int64_t(ts.tv_nsec) / 1000000;
}

uint64_t monotonicMs() { return armTicksToNs(svcGetSystemTick()) / 1000000; }

void logLine(const std::string& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
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
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        std::string dir = path.substr(0, slash);
        size_t pos = 0;
        while ((pos = dir.find('/', pos + 1)) != std::string::npos) {
            mkdir(dir.substr(0, pos).c_str(), 0755);
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
    mkdir("sdmc:/switch", 0755);
    mkdir("sdmc:/switch/telegramnx", 0755);
    return "sdmc:/switch/telegramnx";
}

}  // namespace tgnx::platform

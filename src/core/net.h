// Thin blocking-with-timeout TCP client used by the MTProto transports.
#pragma once

#include <cstdint>
#include <string>

#include "core/util.h"

namespace tgnx::net {

class TcpSocket {
public:
    TcpSocket() = default;
    ~TcpSocket() { close(); }
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    bool connect(const std::string& host, uint16_t port, int timeoutMs, std::string* error);
    bool sendAll(const void* data, size_t size, int timeoutMs, std::string* error);
    bool recvAll(void* data, size_t size, int timeoutMs, std::string* error);
    void close();
    bool valid() const { return fd_ >= 0; }
    int fd() const { return fd_; }

private:
    int fd_ = -1;
};

}  // namespace tgnx::net

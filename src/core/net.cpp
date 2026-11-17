#include "core/net.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "platform/platform.h"

namespace tgnx::net {

namespace {

bool waitFd(int fd, short events, int64_t deadline, std::string* error) {
    for (;;) {
        int64_t now = int64_t(platform::monotonicMs());
        if (now >= deadline) {
            if (error) *error = "timeout";
            return false;
        }
        int remaining = int(deadline - now);
        pollfd pfd{fd, events, 0};
        int rc = ::poll(&pfd, 1, remaining);
        if (rc < 0) {
            if (errno == EINTR) continue;
            if (error) *error = std::string("poll: ") + std::strerror(errno);
            return false;
        }
        if (rc == 0) {
            if (error) *error = "timeout";
            return false;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            int soError = 0;
            socklen_t len = sizeof(soError);
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len);
            if (error) *error = std::string("socket error: ") + std::strerror(soError ? soError : ECONNRESET);
            return false;
        }
        return true;
    }
}

void setNonBlocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

}  // namespace

bool TcpSocket::connect(const std::string& host, uint16_t port, int timeoutMs, std::string* error) {
    close();

    // Numeric IPv4 fast path: libnx's resolver is not needed for the DC
    // addresses, and this also works before any DNS configuration exists.
    sockaddr_in v4{};
    v4.sin_family = AF_INET;
    v4.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &v4.sin_addr) == 1) {
        int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd < 0) {
            if (error) *error = std::string("socket: ") + std::strerror(errno);
            return false;
        }
        setNonBlocking(fd);
        int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&v4), sizeof(v4));
        if (rc != 0 && errno == EINPROGRESS) {
            int64_t deadline = int64_t(platform::monotonicMs()) + timeoutMs;
            if (waitFd(fd, POLLOUT, deadline, error)) {
                int soError = 0;
                socklen_t len = sizeof(soError);
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len);
                rc = soError == 0 ? 0 : -1;
                if (rc != 0 && error) *error = std::string("connect: ") + std::strerror(soError);
            }
        } else if (rc != 0 && error) {
            *error = std::string("connect: ") + std::strerror(errno);
        }
        if (rc == 0) {
            int one = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            fd_ = fd;
            return true;
        }
        ::close(fd);
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    std::string portStr = std::to_string(port);
    int gai = ::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (gai != 0 || !result) {
        if (error) *error = std::string("getaddrinfo: ") + gai_strerror(gai);
        return false;
    }

    int64_t deadline = int64_t(platform::monotonicMs()) + timeoutMs;
    bool connected = false;
    for (addrinfo* ai = result; ai && !connected; ai = ai->ai_next) {
        int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        setNonBlocking(fd);
        int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc == 0) {
            connected = true;
        } else if (errno == EINPROGRESS) {
            if (waitFd(fd, POLLOUT, deadline, error)) {
                int soError = 0;
                socklen_t len = sizeof(soError);
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len);
                if (soError == 0) {
                    connected = true;
                } else if (error) {
                    *error = std::string("connect: ") + std::strerror(soError);
                }
            }
        } else if (error) {
            *error = std::string("connect: ") + std::strerror(errno);
        }
        if (connected) {
            int one = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            fd_ = fd;
        } else {
            ::close(fd);
        }
    }
    ::freeaddrinfo(result);
    if (!connected && error && error->empty()) *error = "connect failed";
    return connected;
}

bool TcpSocket::sendAll(const void* data, size_t size, int timeoutMs, std::string* error) {
    if (fd_ < 0) {
        if (error) *error = "not connected";
        return false;
    }
    const auto* p = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    int64_t deadline = int64_t(platform::monotonicMs()) + timeoutMs;
    while (sent < size) {
        ssize_t n = ::send(fd_, p + sent, size - sent, 0);
        if (n > 0) {
            sent += size_t(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!waitFd(fd_, POLLOUT, deadline, error)) return false;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (error) *error = n < 0 ? std::string("send: ") + std::strerror(errno) : "send: closed";
        return false;
    }
    return true;
}

bool TcpSocket::recvAll(void* data, size_t size, int timeoutMs, std::string* error) {
    if (fd_ < 0) {
        if (error) *error = "not connected";
        return false;
    }
    auto* p = static_cast<uint8_t*>(data);
    size_t got = 0;
    int64_t deadline = int64_t(platform::monotonicMs()) + timeoutMs;
    while (got < size) {
        ssize_t n = ::recv(fd_, p + got, size - got, 0);
        if (n > 0) {
            got += size_t(n);
            continue;
        }
        if (n == 0) {
            if (error) *error = "connection closed by server";
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!waitFd(fd_, POLLIN, deadline, error)) return false;
            continue;
        }
        if (errno == EINTR) continue;
        if (error) *error = std::string("recv: ") + std::strerror(errno);
        return false;
    }
    return true;
}

void TcpSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace tgnx::net

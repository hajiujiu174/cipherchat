#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <mutex>
#include <stdexcept>

namespace cc::net {
#ifdef _WIN32
using Socket = SOCKET;
inline constexpr Socket invalidSocket = INVALID_SOCKET;
#else
using Socket = int;
inline constexpr Socket invalidSocket = -1;
#endif
inline void initializeSockets() {
#ifdef _WIN32
    static std::once_flag once;
    std::call_once(once, [] { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("Winsock initialization failed"); });
#endif
}
inline void closeSocket(Socket socket) noexcept {
    if (socket == invalidSocket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}
inline bool nonblocking(Socket socket) {
#ifdef _WIN32
    u_long enabled = 1; return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const auto flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}
// Limit only this Windows native IPv6 connection. The observed relay path
// drops larger packets until TCP times out and lowers its MSS. Set the cap
// before connect so TCP starts with the usable MSS; never change system MTU.
inline bool setIpv6StreamMtu(Socket socket, const sockaddr* address) {
#ifdef _WIN32
    if (address->sa_family != AF_INET6) return true;
    const auto& ipv6 = reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr;
    if (IN6_IS_ADDR_LOOPBACK(&ipv6) || IN6_IS_ADDR_V4MAPPED(&ipv6)) return true;
    const DWORD mtu = 1280;
    return ::setsockopt(socket, IPPROTO_IPV6, IPV6_USER_MTU,
        reinterpret_cast<const char*>(&mtu), sizeof(mtu)) == 0;
#else
    (void)socket; (void)address;
    return true;
#endif
}
inline int waitSocket(Socket socket, bool read, bool write, int timeoutMs) {
    fd_set reads, writes;
    FD_ZERO(&reads); FD_ZERO(&writes);
    if (read) FD_SET(socket, &reads);
    if (write) FD_SET(socket, &writes);
    timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    return select(static_cast<int>(socket) + 1, read ? &reads : nullptr, write ? &writes : nullptr, nullptr, &timeout);
}
} // namespace cc::net

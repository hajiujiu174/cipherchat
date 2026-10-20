#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "common/net/socket.hpp"

TEST_CASE("Windows native IPv6 connections cap their own transmit MTU before connect") {
    cc::net::initializeSockets();
    const auto socket=::socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
    REQUIRE(socket!=cc::net::invalidSocket);
    sockaddr_in6 address{};address.sin6_family=AF_INET6;
    REQUIRE(inet_pton(AF_INET6,"2001:db8::1234",&address.sin6_addr)==1);
    CHECK(cc::net::setIpv6StreamMtu(socket,reinterpret_cast<const sockaddr*>(&address)));
#ifdef _WIN32
    DWORD value=0;int length=sizeof(value);
    CHECK(::getsockopt(socket,IPPROTO_IPV6,IPV6_USER_MTU,reinterpret_cast<char*>(&value),&length)==0);
    CHECK(value==1280);
    CHECK_FALSE(cc::net::setIpv6StreamMtu(cc::net::invalidSocket,reinterpret_cast<const sockaddr*>(&address)));
#endif
    cc::net::closeSocket(socket);
}
TEST_CASE("IPv4 mapped and IPv6 loopback connections preserve their existing MTU") {
    cc::net::initializeSockets();
    for(const auto* host:{"::1","::ffff:127.0.0.1"}) {
        const auto socket=::socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
        REQUIRE(socket!=cc::net::invalidSocket);
        sockaddr_in6 address{};address.sin6_family=AF_INET6;REQUIRE(inet_pton(AF_INET6,host,&address.sin6_addr)==1);
#ifdef _WIN32
        DWORD before=0,after=0;int length=sizeof(before);
        REQUIRE(::getsockopt(socket,IPPROTO_IPV6,IPV6_USER_MTU,reinterpret_cast<char*>(&before),&length)==0);
#endif
        CHECK(cc::net::setIpv6StreamMtu(socket,reinterpret_cast<const sockaddr*>(&address)));
#ifdef _WIN32
        REQUIRE(::getsockopt(socket,IPPROTO_IPV6,IPV6_USER_MTU,reinterpret_cast<char*>(&after),&length)==0);
        CHECK(after==before);
#endif
        cc::net::closeSocket(socket);
    }
    const auto socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);REQUIRE(socket!=cc::net::invalidSocket);
    sockaddr_in address{};address.sin_family=AF_INET;CHECK(cc::net::setIpv6StreamMtu(socket,reinterpret_cast<const sockaddr*>(&address)));
    cc::net::closeSocket(socket);
}

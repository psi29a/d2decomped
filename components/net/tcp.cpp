// SPDX-License-Identifier: GPL-3.0-or-later
#include "tcp.hpp"

#include <cstring>
#include <string>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace d2d::net {

namespace {

#ifdef _WIN32
using Native = SOCKET;
constexpr Native kNone = INVALID_SOCKET;
auto close_native(Native socket) -> void { ::closesocket(socket); }
auto error_text(int code) -> std::string { return "winsock error " + std::to_string(code); }
auto last_error() -> std::string { return error_text(::WSAGetLastError()); }
auto wait_for(Native socket, short events, int timeout_ms) -> int {
    WSAPOLLFD entry{ socket, events, 0 };
    return ::WSAPoll(&entry, 1, timeout_ms);
}
auto ensure_started() -> bool {
    static const bool started = [] { WSADATA data{}; return ::WSAStartup(MAKEWORD(2, 2), &data) == 0; }();
    return started;
}
auto set_nonblocking(Native socket) -> void { u_long enabled = 1; ::ioctlsocket(socket, FIONBIO, &enabled); }
auto in_progress() -> bool { return ::WSAGetLastError() == WSAEWOULDBLOCK; }
#else
using Native = int;
constexpr Native kNone = -1;
auto close_native(Native socket) -> void { ::close(socket); }
auto error_text(int code) -> std::string { return std::strerror(code); }
auto last_error() -> std::string { return error_text(errno); }
auto wait_for(Native socket, short events, int timeout_ms) -> int {
    pollfd entry{ socket, events, 0 };
    return ::poll(&entry, 1, timeout_ms);
}
auto ensure_started() -> bool { return true; }
auto set_nonblocking(Native socket) -> void { ::fcntl(socket, F_SETFL, ::fcntl(socket, F_GETFL, 0) | O_NONBLOCK); }
auto in_progress() -> bool { return errno == EINPROGRESS; }
#endif

auto native(std::intptr_t socket) -> Native { return static_cast<Native>(socket); }

} // namespace

auto TcpConnection::connect(const std::string& host, std::uint16_t port, int timeout_ms) -> std::expected<TcpConnection, std::string> {
    if (!ensure_started()) return std::unexpected("couldn't start the socket library");
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) return std::unexpected("can't resolve " + host);
    const Native socket = ::socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if (socket == kNone) { ::freeaddrinfo(found); return std::unexpected("socket: " + last_error()); }
    TcpConnection connection(static_cast<std::intptr_t>(socket));
    set_nonblocking(socket);
    const int result = ::connect(socket, found->ai_addr, static_cast<int>(found->ai_addrlen));
    ::freeaddrinfo(found);
    if (result != 0) {
        if (!in_progress()) return std::unexpected("connect: " + last_error());
        if (wait_for(socket, POLLOUT, timeout_ms) <= 0) return std::unexpected("no answer from " + host + " within " + std::to_string(timeout_ms) + " ms");
        int error = 0;
        socklen_t length = sizeof error;
        ::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
        if (error != 0) return std::unexpected("connect: " + error_text(error));
    }
    int enabled = 1;
    ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof enabled);
    return connection;
}

TcpConnection::TcpConnection(TcpConnection&& other) noexcept : socket_(std::exchange(other.socket_, -1)) {}

auto TcpConnection::operator=(TcpConnection&& other) noexcept -> TcpConnection& {
    if (this != &other) {
        if (socket_ != -1) close_native(native(socket_));
        socket_ = std::exchange(other.socket_, -1);
    }
    return *this;
}

TcpConnection::~TcpConnection() {
    if (socket_ != -1) close_native(native(socket_));
}

auto TcpConnection::send(std::span<const std::uint8_t> bytes) -> std::expected<void, std::string> {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto result = ::send(native(socket_), reinterpret_cast<const char*>(bytes.data() + sent), static_cast<int>(bytes.size() - sent), 0);
        if (result > 0) { sent += static_cast<std::size_t>(result); continue; }
        if (wait_for(native(socket_), POLLOUT, 2000) <= 0) return std::unexpected("send: " + last_error());
    }
    return {};
}

auto TcpConnection::receive(int timeout_ms) -> std::expected<d2gs::Bytes, std::string> {
    d2gs::Bytes received;
    if (wait_for(native(socket_), POLLIN, timeout_ms) <= 0) return received;
    std::uint8_t buffer[4096];
    while (true) {
        const auto result = ::recv(native(socket_), reinterpret_cast<char*>(buffer), static_cast<int>(sizeof buffer), 0);
        if (result > 0) { received.insert(received.end(), buffer, buffer + result); continue; }
        if (result == 0) {
            if (received.empty()) return std::unexpected("the other side closed the connection");
            return received;
        }
        return received;   // would block: all there was
    }
}

auto TcpListener::listen(std::uint16_t port) -> std::expected<TcpListener, std::string> {
    if (!ensure_started()) return std::unexpected("couldn't start the socket library");
    const Native socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == kNone) return std::unexpected("socket: " + last_error());
    TcpListener listener(static_cast<std::intptr_t>(socket));
    int enabled = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof enabled);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) return std::unexpected("bind port " + std::to_string(port) + ": " + last_error());
    if (::listen(socket, 4) != 0) return std::unexpected("listen: " + last_error());
    return listener;
}

TcpListener::TcpListener(TcpListener&& other) noexcept : socket_(std::exchange(other.socket_, -1)) {}

auto TcpListener::operator=(TcpListener&& other) noexcept -> TcpListener& {
    if (this != &other) {
        if (socket_ != -1) close_native(native(socket_));
        socket_ = std::exchange(other.socket_, -1);
    }
    return *this;
}

TcpListener::~TcpListener() {
    if (socket_ != -1) close_native(native(socket_));
}

auto TcpListener::accept(int timeout_ms, std::string* from) -> std::expected<std::optional<TcpConnection>, std::string> {
    if (wait_for(native(socket_), POLLIN, timeout_ms) <= 0) return std::optional<TcpConnection>{};
    sockaddr_in address{};
    socklen_t length = sizeof address;
    const Native client = ::accept(native(socket_), reinterpret_cast<sockaddr*>(&address), &length);
    if (client == kNone) return std::unexpected("accept: " + last_error());
    if (from) {
        const auto ip = ntohl(address.sin_addr.s_addr);
        *from = std::to_string(ip >> 24) + "." + std::to_string(ip >> 16 & 0xff) + "." + std::to_string(ip >> 8 & 0xff) + "." + std::to_string(ip & 0xff);
    }
    set_nonblocking(client);
    int enabled = 1;
    ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof enabled);
    return std::optional<TcpConnection>{ TcpConnection(static_cast<std::intptr_t>(client)) };
}

} // namespace d2d::net

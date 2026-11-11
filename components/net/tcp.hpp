// SPDX-License-Identifier: GPL-3.0-or-later
// A TCP connection to a game host, polled from the game loop: no thread,
// no blocking past the timeout the caller gives. TCP_NODELAY like
// game.exe (packets are small and timely). POSIX sockets or Winsock.
#pragma once

#include <d2gs/wire.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace d2d::net {

class TcpConnection {
public:
    // Resolves `host` and connects within `timeout_ms`.
    static auto connect(const std::string& host, std::uint16_t port, int timeout_ms) -> std::expected<TcpConnection, std::string>;

    TcpConnection(TcpConnection&& other) noexcept;
    auto operator=(TcpConnection&& other) noexcept -> TcpConnection&;
    TcpConnection(const TcpConnection&) = delete;
    auto operator=(const TcpConnection&) -> TcpConnection& = delete;
    ~TcpConnection();

    // All of `bytes` sent, or what the socket wouldn't take yet kept to go
    // first thing on the next send or receive: never a wait. The error once
    // the socket failed, or when a peer that isn't reading has kMaxPending
    // bytes waiting (the connection is failed from then on).
    auto send(std::span<const std::uint8_t> bytes) -> std::expected<void, std::string>;
    // What arrived within `timeout_ms` (0: only what's there now); empty
    // when nothing did. An error once the host closed or the socket failed.
    auto receive(int timeout_ms) -> std::expected<d2gs::Bytes, std::string>;
    // Closes after a last send: what's waiting goes (within a moment), the
    // other side gets it and a close, not a reset (a close with unread data
    // resets on Winsock and drops what was sent).
    auto finish() -> void;
    // finish() a step at a time, never a wait (the host's game loop): what's
    // waiting goes as the socket takes it, then the close; true once it's
    // closed (the other side closed too, or the connection failed).
    auto finish_step() -> bool;
    static constexpr std::size_t kMaxPending = 8u << 20;

private:
    friend class TcpListener;
    explicit TcpConnection(std::intptr_t socket) : socket_(socket) {}
    // Sends what's pending while the socket takes it; false once the connection has failed.
    auto flush() -> bool;
    std::intptr_t socket_ = -1;
    d2gs::Bytes pending_;
    std::string failed_;
    bool shut_ = false;    // our side's close sent (finish_step)   // why the connection is over (the error every call returns from then on)
};

// A listening socket (SO_REUSEADDR, like game.exe's host), on one IPv4
// address or every one ("0.0.0.0").
class TcpListener {
public:
    static auto listen(std::uint16_t port, const std::string& bind_address = "0.0.0.0") -> std::expected<TcpListener, std::string>;
    TcpListener(TcpListener&& other) noexcept;
    auto operator=(TcpListener&& other) noexcept -> TcpListener&;
    TcpListener(const TcpListener&) = delete;
    auto operator=(const TcpListener&) -> TcpListener& = delete;
    ~TcpListener();
    // A client that connected within `timeout_ms` (empty: none), and its address.
    auto accept(int timeout_ms, std::string* from = nullptr) -> std::expected<std::optional<TcpConnection>, std::string>;

private:
    explicit TcpListener(std::intptr_t socket) : socket_(socket) {}
    std::intptr_t socket_ = -1;
};

// This machine's IPv4 address as game.exe's TCP/IP screen shows it
// (FUN_0040df60): the host name's first address, else the one a UDP
// "connect" to 24.105.29.30:7 picks (nothing is sent), else "0.0.0.0".
auto own_address() -> std::string;

} // namespace d2d::net

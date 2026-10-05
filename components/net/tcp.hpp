// SPDX-License-Identifier: GPL-3.0-or-later
// A TCP connection to a game host, polled from the game loop: no thread,
// no blocking past the timeout the caller gives. TCP_NODELAY like
// game.exe (packets are small and timely). POSIX sockets or Winsock.
#pragma once

#include <d2gs/wire.hpp>

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

    // All of `bytes`, or the error.
    auto send(std::span<const std::uint8_t> bytes) -> std::expected<void, std::string>;
    // What arrived within `timeout_ms` (0: only what's there now); empty
    // when nothing did. An error once the host closed or the socket failed.
    auto receive(int timeout_ms) -> std::expected<d2gs::Bytes, std::string>;

private:
    friend class TcpListener;
    explicit TcpConnection(std::intptr_t socket) : socket_(socket) {}
    std::intptr_t socket_ = -1;
};

// A listening socket on every address (SO_REUSEADDR, like game.exe's host).
class TcpListener {
public:
    static auto listen(std::uint16_t port) -> std::expected<TcpListener, std::string>;
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

} // namespace d2d::net

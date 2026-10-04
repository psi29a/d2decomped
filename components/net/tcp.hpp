// SPDX-License-Identifier: GPL-3.0-or-later
// A TCP connection to a game host, polled from the game loop: no thread,
// no blocking past the timeout the caller gives. TCP_NODELAY like
// game.exe (packets are small and timely). POSIX sockets or Winsock.
#pragma once

#include <d2gs/wire.hpp>

#include <cstdint>
#include <expected>
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
    explicit TcpConnection(std::intptr_t socket) : socket_(socket) {}
    std::intptr_t socket_ = -1;
};

} // namespace d2d::net

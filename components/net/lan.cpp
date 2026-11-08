// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for lan.hpp.
#include "lan.hpp"

#include <tcp.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace d2d::net::lan {

namespace {

using Clock = std::chrono::steady_clock;

auto put_u32(Bytes& out, std::uint32_t value) -> void {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(std::uint8_t(value >> shift));
}
auto put_u64(Bytes& out, std::uint64_t value) -> void {
    for (int shift = 0; shift < 64; shift += 8) out.push_back(std::uint8_t(value >> shift));
}
auto get_u32(std::span<const std::uint8_t> bytes, std::size_t offset) -> std::uint32_t {
    std::uint32_t value = 0;
    for (int byte = 0; byte < 4; ++byte) value |= std::uint32_t(bytes[offset + std::size_t(byte)]) << (8 * byte);
    return value;
}
auto now_ms() -> std::uint64_t {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
}
auto known(std::uint8_t kind) -> bool { return kind >= std::uint8_t(Kind::Hello) && kind <= std::uint8_t(Kind::Resync); }

auto send_frame(TcpConnection& connection, Kind kind, std::span<const std::uint8_t> payload) -> bool {
    return connection.send(encode_frame(kind, payload)).has_value();
}
auto send_text(TcpConnection& connection, Kind kind, const std::string& text) -> bool {
    return send_frame(connection, kind, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}
auto refuse(TcpConnection& connection, const std::string& text) -> void {
    send_text(connection, Kind::Refuse, text);   // Host::retire closes it after
}
auto send_ping(TcpConnection& connection) -> bool {
    Bytes stamp; put_u64(stamp, now_ms());
    return send_frame(connection, Kind::Ping, stamp);
}

}  // namespace

auto encode_frame(Kind kind, std::span<const std::uint8_t> payload) -> Bytes {
    Bytes out;
    out.reserve(5 + payload.size());
    put_u32(out, std::uint32_t(payload.size()));
    out.push_back(std::uint8_t(kind));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

auto FrameReader::feed(std::span<const std::uint8_t> bytes) -> std::expected<void, std::string> {
    if (start_ > 0 && start_ >= buffer_.size() / 2) {   // drop what's been read, now and then
        buffer_.erase(buffer_.begin(), buffer_.begin() + std::ptrdiff_t(start_));
        start_ = 0;
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    // Check the next header as soon as it's whole, so a bad stream fails here, not later.
    if (buffer_.size() - start_ >= 5) {
        const auto length = get_u32(buffer_, start_);
        const auto kind = buffer_[start_ + 4];
        if (length > kMaxPayload) return std::unexpected("frame of " + std::to_string(length) + " bytes");
        if (!known(kind)) return std::unexpected("unknown frame kind " + std::to_string(kind));
    }
    return {};
}

auto FrameReader::next() -> std::optional<Frame> {
    if (buffer_.size() - start_ < 5) return std::nullopt;
    const auto length = get_u32(buffer_, start_);
    const auto kind = buffer_[start_ + 4];
    if (length > kMaxPayload || !known(kind)) return std::nullopt;
    if (buffer_.size() - start_ - 5 < length) return std::nullopt;
    Frame frame{ Kind(kind), Bytes(buffer_.begin() + std::ptrdiff_t(start_ + 5), buffer_.begin() + std::ptrdiff_t(start_ + 5 + length)) };
    start_ += 5 + length;
    return frame;
}

auto encode_hello(const Hello& hello) -> Bytes {
    Bytes out;
    put_u32(out, hello.version);
    put_u32(out, std::uint32_t(hello.name.size()));
    out.insert(out.end(), hello.name.begin(), hello.name.end());
    put_u32(out, std::uint32_t(hello.save.size()));
    out.insert(out.end(), hello.save.begin(), hello.save.end());
    return out;
}

auto decode_hello(std::span<const std::uint8_t> payload) -> std::optional<Hello> {
    Hello hello;
    std::size_t offset = 0;
    auto u32 = [&](std::uint32_t& out) { if (payload.size() - offset < 4) return false; out = get_u32(payload, offset); offset += 4; return true; };
    std::uint32_t name_size = 0, save_size = 0;
    if (!u32(hello.version) || !u32(name_size) || name_size > 64 || payload.size() - offset < name_size) return std::nullopt;
    hello.name.assign(reinterpret_cast<const char*>(payload.data() + offset), name_size); offset += name_size;
    if (!u32(save_size) || payload.size() - offset != save_size) return std::nullopt;
    hello.save.assign(payload.begin() + std::ptrdiff_t(offset), payload.end());
    return hello;
}

auto Host::listen(std::uint16_t port, const std::string& bind_address) -> std::expected<Host, std::string> {
    auto listener = TcpListener::listen(port, bind_address);
    if (!listener) return std::unexpected(listener.error());
    return Host(std::move(*listener));
}

Host::~Host() {
    for (auto& client : clients_) {
        if (client.joined) send_frame(client.connection, Kind::Bye, {});
        client.connection.finish();
    }
    for (auto& closing : closing_) closing.connection.finish();
}

auto Host::retire(TcpConnection&& connection) -> void {
    closing_.push_back({ std::move(connection), Clock::now() + std::chrono::milliseconds(2500) });
}

auto Host::drop(std::size_t index, const std::string& reason, std::vector<Event>& events) -> void {
    if (clients_[index].joined && !clients_[index].leaving) events.push_back({ .type = Event::Type::Left, .client = clients_[index].number, .reason = reason });
    clients_.erase(clients_.begin() + std::ptrdiff_t(index));
}

auto Host::poll() -> std::vector<Event> {
    std::vector<Event> events;
    const auto now = Clock::now();
    std::erase_if(closing_, [&](Closing& closing) { return closing.connection.finish_step() || now > closing.until; });
    // Newcomers: one a poll is plenty; a full game turns them away.
    if (auto accepted = listener_.accept(0); accepted && *accepted) {
        Client client{ .number = next_id_++, .connection = std::move(**accepted), .reader = {}, .joined = false, .heard = now, .pinged = now };
        const auto seated = std::count_if(clients_.begin(), clients_.end(), [](const Client& other) { return !other.leaving; });
        if (seated >= kMaxClients) {   // one saying goodbye has given up its seat
            refuse(client.connection, "the game is full");
            retire(std::move(client.connection));
        } else {
            clients_.push_back(std::move(client));
        }
    }
    for (std::size_t index = 0; index < clients_.size();) {
        auto& client = clients_[index];
        auto received = client.connection.receive(0);
        if (!received) { drop(index, received.error(), events); continue; }
        if (!received->empty()) {
            client.heard = now;
            if (auto fed = client.reader.feed(*received); !fed) { drop(index, fed.error(), events); continue; }
        }
        bool gone = false;
        while (auto frame = client.reader.next()) {
            switch (frame->kind) {
                case Kind::Hello: {
                    auto hello = decode_hello(frame->payload);
                    if (!hello || client.joined) { gone = true; refuse(client.connection, "bad hello"); retire(std::move(client.connection)); break; }
                    if (hello->version != kProtocolVersion) {
                        refuse(client.connection, "this host speaks LAN protocol " + std::to_string(kProtocolVersion) + ", you " + std::to_string(hello->version));
                        retire(std::move(client.connection));
                        gone = true; break;
                    }
                    client.joined = true;
                    Bytes player; put_u32(player, std::uint32_t(client.number));
                    player.insert(player.end(), welcome_info.begin(), welcome_info.end());
                    send_frame(client.connection, Kind::Welcome, player);
                    events.push_back({ .type = Event::Type::Joined, .client = client.number, .hello = std::move(*hello) });
                    break;
                }
                case Kind::Command:
                    if (client.joined) events.push_back({ .type = Event::Type::Command, .client = client.number, .command = std::move(frame->payload) });
                    break;
                case Kind::Resync:
                    if (client.joined) events.push_back({ .type = Event::Type::Resync, .client = client.number });
                    break;
                case Kind::Ping: send_frame(client.connection, Kind::Pong, frame->payload); break;
                case Kind::Pong: break;   // heard: that's what it's for
                case Kind::Bye:
                    if (client.joined && !client.leaving) { client.leaving = true; events.push_back({ .type = Event::Type::Left, .client = client.number, .reason = "left", .goodbye = true }); }
                    break;
                default: gone = true; break;   // Welcome, View, Refuse, Save: not a client's to send
            }
            if (gone) break;
        }
        if (gone) { drop(index, "left", events); continue; }
        if (client.leaving) { if (now - client.heard > kTimeout) clients_.erase(clients_.begin() + std::ptrdiff_t(index)); else ++index; continue; }
        if (now - client.heard > kTimeout) { drop(index, "timed out", events); continue; }
        if (client.joined && now - client.pinged >= kPingEvery) {
            client.pinged = now;
            if (!send_ping(client.connection)) { drop(index, "send failed", events); continue; }
        }
        ++index;
    }
    return events;
}

auto Host::send_view(int client, std::span<const std::uint8_t> view) -> bool {
    const auto found = std::ranges::find(clients_, client, &Client::number);
    if (found == clients_.end() || !found->joined) return false;
    return send_frame(found->connection, Kind::View, view);
}

auto Host::kick(int client, const std::string& reason) -> void {
    const auto found = std::ranges::find(clients_, client, &Client::number);
    if (found == clients_.end()) return;
    refuse(found->connection, reason);
    retire(std::move(found->connection));
    clients_.erase(found);
}

auto Host::send_save(int client, std::span<const std::uint8_t> save) -> bool {
    const auto found = std::ranges::find(clients_, client, &Client::number);
    if (found == clients_.end() || !found->joined) return false;
    return send_frame(found->connection, Kind::Save, save);
}

auto Host::close(int client) -> void {
    const auto found = std::ranges::find(clients_, client, &Client::number);
    if (found == clients_.end()) return;
    retire(std::move(found->connection));
    clients_.erase(found);
}

auto Host::clients() const -> std::vector<int> {
    std::vector<int> joined;
    for (const auto& client : clients_) if (client.joined && !client.leaving) joined.push_back(client.number);
    return joined;
}

auto Client::join(const std::string& host, Hello hello, std::uint16_t port, int timeout_ms) -> std::expected<Client, std::string> {
    auto connection = TcpConnection::connect(host, port, timeout_ms);
    if (!connection) return std::unexpected(connection.error());
    Client client(std::move(*connection));
    hello.version = kProtocolVersion;
    if (!send_frame(client.connection_, Kind::Hello, encode_hello(hello))) return std::unexpected("couldn't send the hello");
    client.heard_ = client.pinged_ = Clock::now();
    return client;
}

auto Client::poll() -> std::vector<Event> {
    std::vector<Event> events;
    if (!connected_) return events;
    const auto now = Clock::now();
    auto lost = [&](std::string reason) { connected_ = false; events.push_back({ .type = Event::Type::Lost, .reason = std::move(reason) }); };
    auto received = connection_.receive(0);
    if (!received) { lost(received.error()); return events; }
    if (!received->empty()) {
        heard_ = now;
        if (auto fed = reader_.feed(*received); !fed) { lost(fed.error()); return events; }
    }
    while (auto frame = reader_.next()) {
        switch (frame->kind) {
            case Kind::Welcome:
                if (frame->payload.size() >= 4)
                    events.push_back({ .type = Event::Type::Welcome, .player = int(get_u32(frame->payload, 0)), .info = Bytes(frame->payload.begin() + 4, frame->payload.end()) });
                break;
            case Kind::View: events.push_back({ .type = Event::Type::View, .view = std::move(frame->payload) }); break;
            case Kind::Save: events.push_back({ .type = Event::Type::Save, .view = std::move(frame->payload) }); break;
            case Kind::Refuse:
                connected_ = false;
                events.push_back({ .type = Event::Type::Refused, .reason = std::string(frame->payload.begin(), frame->payload.end()) });
                return events;
            case Kind::Ping: send_frame(connection_, Kind::Pong, frame->payload); break;
            case Kind::Pong: break;
            case Kind::Bye: lost("the host left"); return events;
            default: lost("unexpected frame from the host"); return events;
        }
    }
    if (now - heard_ > kTimeout) { lost("the host stopped answering"); return events; }
    if (now - pinged_ >= kPingEvery) { pinged_ = now; if (!send_ping(connection_)) lost("send failed"); }
    return events;
}

auto Client::send_resync() -> bool {
    if (!connected_) return false;
    if (!send_frame(connection_, Kind::Resync, {})) { connected_ = false; return false; }
    return true;
}

auto Client::send_command(std::span<const std::uint8_t> command) -> bool {
    if (!connected_) return false;
    if (!send_frame(connection_, Kind::Command, command)) { connected_ = false; return false; }
    return true;
}

auto Client::leave() -> void {
    if (!connected_) return;
    if (!send_frame(connection_, Kind::Bye, {})) connected_ = false;
}

}  // namespace d2d::net::lan

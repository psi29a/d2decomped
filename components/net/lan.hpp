// SPDX-License-Identifier: GPL-3.0-or-later
// d2d to d2d over a LAN: one d2d hosts the game (its World), others join
// and play as further World players. The transport only frames bytes; the
// payloads are the in-process wire forms already (Command: protocol.hpp,
// View: replication.hpp), so a joined client is the single-player client
// with a socket in place of LocalTransport. Not game.exe's D2GS protocol
// (docs/design/net-host.md option (a)): d2d clients only.
//
// A frame: u32 payload length (little endian), u8 kind, the payload.
// Polled from the game loop like TcpConnection: no threads.
#pragma once

#include <tcp.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace d2d::net::lan {

using Bytes = std::vector<std::uint8_t>;

// Bumped whenever a frame or payload changes shape: a host refuses other versions.
inline constexpr std::uint32_t kProtocolVersion = 5;   // 2: Save, the goodbye that waits for it; 3: Resync; 4: Say, Message (stage 9); 5: a unit's place in 16.16
inline constexpr std::uint16_t kDefaultPort = 6113;      // game.exe hosts on 4000; this is a different protocol
inline constexpr std::size_t kMaxPayload = 8u << 20;     // a View or a save is far smaller
inline constexpr int kMaxClients = 3;                    // the host plays too: four players
inline constexpr auto kPingEvery = std::chrono::milliseconds(1000);
inline constexpr auto kTimeout = std::chrono::milliseconds(5000);

enum class Kind : std::uint8_t {
    Hello = 1,     // client -> host: version, name, character save
    Welcome = 2,   // host -> client: the player id it got
    Refuse = 3,    // host -> client: why not (text), then the host closes
    Command = 4,   // client -> host: one encoded Command
    View = 5,      // host -> client: the client's View, encoded
    Ping = 6,      // either way: u64 send time (ms); the other side echoes it as Pong (a silent side is dropped)
    Pong = 7,
    Bye = 8,       // either way: leaving on purpose (a joiner's: the host answers with a Save, then closes; the host's: on its way out)
    Save = 9,      // host -> client: the joiner's character, a whole .d2s
    Resync = 10,   // client -> host: a View didn't apply; the next one whole, please
};

struct Frame { Kind kind{}; Bytes payload; };

auto encode_frame(Kind kind, std::span<const std::uint8_t> payload) -> Bytes;

// Turns a byte stream back into frames, however TCP cut it up.
class FrameReader {
public:
    // Adds received bytes. An error (and the reader stops) on an unknown kind or an oversized frame.
    auto feed(std::span<const std::uint8_t> bytes) -> std::expected<void, std::string>;
    // The next whole frame, if one has arrived.
    auto next() -> std::optional<Frame>;
private:
    Bytes buffer_;
    std::size_t start_ = 0;
};

struct Hello { std::uint32_t version = kProtocolVersion; std::string name; Bytes save; };
auto encode_hello(const Hello& hello) -> Bytes;
auto decode_hello(std::span<const std::uint8_t> payload) -> std::optional<Hello>;

// The host's side: listens, accepts up to kMaxClients, reads their frames.
class Host {
public:
    struct Event {
        enum class Type { Joined, Command, Left, Resync } type{};
        int client = -1;            // the host's id for the connection (also the Welcome's player id)
        Hello hello;                // Joined
        Bytes command;              // Command: one encoded Command
        std::string reason;         // Left: why
        bool goodbye = false;       // Left by Bye: still connected, for a last send_save, then close()
    };
    static auto listen(std::uint16_t port = kDefaultPort, const std::string& bind_address = "0.0.0.0") -> std::expected<Host, std::string>;
    Host(Host&&) noexcept = default;
    auto operator=(Host&&) noexcept -> Host& = default;
    // Says Bye to everyone still connected and closes (lan_host.reset() on the way out).
    ~Host();
    // Accepts, reads, pings, times out; what happened since the last poll, in order.
    auto poll() -> std::vector<Event>;
    // The client's latest View. False when that client has gone.
    auto send_view(int client, std::span<const std::uint8_t> view) -> bool;
    auto kick(int client, const std::string& reason) -> void;
    auto send_save(int client, std::span<const std::uint8_t> save) -> bool;
    // Ends a connection without a word (after a goodbye's last Save, which gets there first).
    auto close(int client) -> void;
    // Sent with each Welcome after the player id: what a joiner needs to make the same game (lan-host.md).
    Bytes welcome_info;
    [[nodiscard]] auto clients() const -> std::vector<int>;
private:
    struct Client {
        int number = -1;
        TcpConnection connection;
        FrameReader reader;
        bool joined = false;
        bool leaving = false;       // said Bye: kept for a last Save until close()
        std::chrono::steady_clock::time_point heard, pinged;
    };
    explicit Host(TcpListener listener) : listener_(std::move(listener)) {}
    auto drop(std::size_t index, const std::string& reason, std::vector<Event>& events) -> void;
    // A connection being closed: poll() finishes it a step at a time (no
    // stall for the players), shut by `until` whatever the other side does.
    struct Closing { TcpConnection connection; std::chrono::steady_clock::time_point until; };
    auto retire(TcpConnection&& connection) -> void;
    TcpListener listener_;
    std::vector<Client> clients_;
    std::vector<Closing> closing_;
    int next_id_ = 1;   // 0 is the host's own player
};

// A joining d2d's side.
class Client {
public:
    struct Event {
        enum class Type { Welcome, View, Save, Refused, Lost } type{};
        int player = -1;            // Welcome
        Bytes info;                 // Welcome: the host's welcome_info
        Bytes view;                 // View; Save: the .d2s
        std::string reason;         // Refused / Lost
    };
    // Connects and says hello (`hello.version` is set here).
    static auto join(const std::string& host, Hello hello, std::uint16_t port = kDefaultPort, int timeout_ms = 3000) -> std::expected<Client, std::string>;
    auto poll() -> std::vector<Event>;
    auto send_command(std::span<const std::uint8_t> command) -> bool;
    // Lost track of the Views: the host's next is whole (its encoder reset).
    auto send_resync() -> bool;
    // Says goodbye; poll on for the host's last Save, then the host closes (Lost).
    auto leave() -> void;
    [[nodiscard]] auto connected() const -> bool { return connected_; }
private:
    explicit Client(TcpConnection connection) : connection_(std::move(connection)) {}
    TcpConnection connection_;
    FrameReader reader_;
    bool connected_ = true;
    std::chrono::steady_clock::time_point heard_, pinged_;
};

}  // namespace d2d::net::lan

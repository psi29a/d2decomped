// SPDX-License-Identifier: GPL-3.0-or-later
// d2proxy: a network debug proxy between a game.exe (or d2d) client and a
// game.exe TCP/IP host. The client connects here instead of to the host;
// every byte goes through unchanged, and both directions are decoded
// (the host's: frames, Huffman, the splitter; the client's: the C->S
// sizes) and logged:
//   error    the connection, a stream that can't be decoded
//   warning  a packet whose id nobody has looked at (its bytes too)
//   info     connections, the join, the leave, a summary   (the default)
//   debug    every packet: direction, id, name, size
//   trace    every packet's bytes, every read's size
// The whole stream also goes to a net log (net_log.hpp's format): one file
// a session in --log-dir (d2proxy-<UTC>-<client>.log, beside d2proxy.log,
// the console's lines), or every session appended to --log.
// Tables come from your own game.exe. Only ever between machines of yours:
// it answers private and loopback addresses only unless --allow says
// otherwise, on --bind's address (all by default); one client at a time,
// dropped after --idle seconds without a byte either way.
//   d2proxy --host 192.168.50.7 [--port 4000] [--listen 4000] [--level info]
//           [--bind 0.0.0.0] [--allow 192.168.50.0/24,...] [--idle 600]
//           [--game-exe PATH] [--log-dir DIR | --log FILE]
#include <allow.hpp>
#include <d2gs/c2s_names.hpp>
#include <d2gs/exe_tables.hpp>
#include <d2gs/huffman.hpp>
#include <d2gs/s2c_names.hpp>
#include <d2gs/split.hpp>
#include <d2gs/wire.hpp>
#include <log.hpp>
#include <net_log.hpp>
#include <tcp.hpp>

#include <CLI/CLI.hpp>

#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using d2d::net::d2gs::Bytes;

auto hex(std::span<const std::uint8_t> bytes) -> std::string {
    std::string out;
    out.reserve(bytes.size() * 3);
    for (const auto byte : bytes) out += std::format("{}{:02x}", out.empty() ? "" : " ", byte);
    return out;
}

// One direction's packets: counted, logged at their level, into the file.
struct Direction {
    std::string_view arrow;                  // "S>C" / "C>S"
    std::string_view (*name)(std::uint8_t);
    std::array<std::uint64_t, 256> counts{};

    auto packet(std::span<const std::uint8_t> bytes, d2d::net::NetLog& file, bool from_host) -> void {
        if (bytes.empty()) return;
        const auto id = bytes[0];
        ++counts[id];
        if (from_host) file.from_host(bytes);
        else file.to_host(bytes);
        const auto label = name(id);
        if (label.empty()) d2d::log::warn("{} {:02x} unknown ({} bytes): {}", arrow, id, bytes.size(), hex(bytes));
        else d2d::log::debug("{} {:02x} {} ({} bytes)", arrow, id, label, bytes.size());
        d2d::log::trace("{} {:02x} {}", arrow, id, hex(bytes));
    }

    auto summary() const -> void {
        std::string line;
        for (std::size_t id = 0; id < counts.size(); ++id)
            if (counts[id]) line += std::format(" {:02x}{}x{}", id, name(std::uint8_t(id)).empty() ? "?" : "", counts[id]);
        d2d::log::info("  {}:{}", arrow, line.empty() ? " none" : line);
    }
};

// A client's session: until either side closes.
auto relay(d2d::net::TcpConnection& client, const std::string& host, std::uint16_t port, const d2d::net::d2gs::ExeTables& tables,
           const d2d::net::d2gs::Huffman& huffman, const std::filesystem::path& log_path, bool append, int idle_seconds) -> void {
    auto upstream = d2d::net::TcpConnection::connect(host, port, 5000);
    if (!upstream) { d2d::log::error("host {}:{}: {}", host, port, upstream.error()); return; }
    d2d::log::info("  connected to {}:{}; net log {}", host, port, log_path.string());
    d2d::net::NetLog file(log_path, {}, append);
    if (!file.is_open()) d2d::log::error("can't write the net log {}", log_path.string());
    d2d::net::d2gs::Receiver from_host(huffman, tables.s2c_sizes);
    d2d::net::d2gs::C2sSplitter from_client(tables.c2s_sizes);
    Direction from_host_dir{ "S>C", &d2d::net::d2gs::s2c_name }, from_client_dir{ "C>S", &d2d::net::d2gs::c2s_name };
    bool host_broken = false, client_broken = false;
    auto last_traffic = std::chrono::steady_clock::now();
    while (true) {
        if (std::chrono::steady_clock::now() - last_traffic > std::chrono::seconds(idle_seconds)) {
            d2d::log::warn("  no traffic for {} s: dropping the session", idle_seconds);
            break;
        }
        // The client's bytes: to the host as they are, then decoded.
        auto sent = client.receive(5);
        if (!sent) { d2d::log::info("  the client closed ({})", sent.error()); break; }
        if (!sent->empty()) {
            last_traffic = std::chrono::steady_clock::now();
            d2d::log::trace("C>S read {} bytes", sent->size());
            if (auto forwarded = upstream->send(*sent); !forwarded) { d2d::log::error("to the host: {}", forwarded.error()); break; }
            if (!client_broken) {
                from_client.append(*sent);
                while (true) {
                    auto packet = from_client.next();
                    if (!packet) { d2d::log::error("C>S stream: {} (relaying on, not decoding)", packet.error().reason); client_broken = true; break; }
                    if (!*packet) break;
                    const auto& bytes = **packet;
                    if (bytes[0] == 0x68 && bytes.size() >= 0x25)
                        d2d::log::info("  join as {} (class {})", d2d::net::d2gs::read_name(bytes, 0x15, 16), bytes[7]);
                    if (bytes[0] == 0x69) d2d::log::info("  the client leaves");
                    from_client_dir.packet(bytes, file, false);
                }
            }
        }
        // The host's: to the client as they are, then decoded.
        auto received = upstream->receive(5);
        if (!received) { d2d::log::info("  the host closed ({})", received.error()); break; }
        if (!received->empty()) {
            last_traffic = std::chrono::steady_clock::now();
            d2d::log::trace("S>C read {} bytes", received->size());
            if (auto forwarded = client.send(*received); !forwarded) { d2d::log::error("to the client: {}", forwarded.error()); break; }
            if (!host_broken) {
                auto packets = from_host.receive(*received);
                if (!packets) {
                    d2d::log::error("S>C stream: {} (relaying on, not decoding)", packets.error().reason);
                    host_broken = true;
                } else {
                    for (const auto& bytes : *packets) {
                        if (bytes[0] == 0x04) d2d::log::info("  in the game");
                        if (bytes[0] == 0xb4 && bytes.size() >= 5) d2d::log::warn("the host refused the join: reason {:#x}", d2d::net::d2gs::read_u32(bytes, 1));
                        from_host_dir.packet(bytes, file, true);
                    }
                }
            }
        }
    }
    d2d::log::info("  session over; packets by id (? = unknown):");
    from_client_dir.summary();
    from_host_dir.summary();
    file.summary();
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{ "d2proxy: a network debug proxy between a Diablo II client and a game.exe TCP/IP host" };
    std::string host, level_name = "info", game_exe, log_path, log_dir, bind_address = "0.0.0.0";
    std::string allow_list = "10.0.0.0/8,172.16.0.0/12,192.168.0.0/16,127.0.0.0/8";
    int port = 4000, listen_port = 4000, idle_seconds = 600;
    app.add_option("--host", host, "The game.exe host to relay to (your own network only)")->required();
    app.add_option("--port", port, "Its port")->check(CLI::Range(1, 65535));
    app.add_option("--listen", listen_port, "Where the client connects")->check(CLI::Range(1, 65535));
    app.add_option("--level", level_name, "error, warning, info, debug or trace");
    app.add_option("--bind", bind_address, "The IPv4 address to listen on (0.0.0.0: every one)");
    app.add_option("--allow", allow_list, "Clients let in: addresses or ranges (a.b.c.d/bits), comma separated (default: private and loopback)");
    app.add_option("--idle", idle_seconds, "Drop a session after this many seconds without traffic")->check(CLI::Range(5, 86400));
    app.add_option("--game-exe", game_exe, "Your 1.14d game.exe, for its tables (else $D2_GAME_EXE)");
    auto* dir_option = app.add_option("--log-dir", log_dir, "Where the logs go: a net log a session and d2proxy.log (else here)");
    app.add_option("--log", log_path, "One net log file for every session, appended to")->excludes(dir_option);
    CLI11_PARSE(app, argc, argv);
    if (log_path.empty()) {
        std::error_code error;
        if (log_dir.empty()) log_dir = ".";
        std::filesystem::create_directories(log_dir, error);
        if (error) { d2d::log::error("--log-dir {}: {}", log_dir, error.message()); return 2; }
        d2d::log::open(std::filesystem::path(log_dir) / "d2proxy.log", true);   // kept across restarts
    }
    const auto level = d2d::log::parse_level(level_name);
    if (!level) { d2d::log::error("--level {}: one of error, warning, info, debug, trace", level_name); return 2; }
    d2d::log::set_level(*level);
    if (game_exe.empty()) if (const char* env = std::getenv("D2_GAME_EXE")) game_exe = env;
    if (game_exe.empty()) { d2d::log::error("no game.exe (--game-exe or D2_GAME_EXE)"); return 2; }
    const auto tables = d2d::net::d2gs::load_exe_tables(game_exe);
    if (!tables) { d2d::log::error("{}", tables.error()); return 1; }
    const auto huffman = d2d::net::d2gs::Huffman::build(tables->lengths);
    if (!huffman) { d2d::log::error("{}", huffman.error()); return 1; }
    std::vector<d2d::net::Range> ranges;
    for (std::string_view rest = allow_list; !rest.empty();) {
        const auto comma = rest.find(',');
        const auto entry = rest.substr(0, comma);
        const auto range = d2d::net::parse_range(entry);
        if (!range) { d2d::log::error("--allow {}: not an address or a.b.c.d/bits range", entry); return 2; }
        ranges.push_back(*range);
        if (comma == std::string_view::npos) break;
        rest.remove_prefix(comma + 1);
    }
    auto listener = d2d::net::TcpListener::listen(std::uint16_t(listen_port), bind_address);
    if (!listener) { d2d::log::error("{}", listener.error()); return 1; }
    d2d::log::info("d2proxy: clients from {} on {}:{} go to {}:{} (level {}, idle {} s)", allow_list, bind_address, listen_port, host, port, level_name, idle_seconds);
    while (true) {
        std::string from;
        auto client = listener->accept(1000, &from);
        if (!client) { d2d::log::error("{}", client.error()); return 1; }
        if (!*client) continue;
        if (!d2d::net::allowed(ranges, from)) { d2d::log::warn("client {} refused: not in --allow", from); continue; }   // closed as it goes out of scope
        d2d::log::info("client {} connected", from);
        const auto session_log = !log_path.empty() ? std::filesystem::path(log_path)
            : std::filesystem::path(log_dir) / std::format("d2proxy-{:%Y%m%dT%H%M%SZ}-{}.log", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()), from);
        relay(**client, host, std::uint16_t(port), *tables, *huffman, session_log, !log_path.empty(), idle_seconds);
    }
}

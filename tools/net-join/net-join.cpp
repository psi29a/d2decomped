// SPDX-License-Identifier: GPL-3.0-or-later
// Join a game.exe TCP/IP host, stay a while, leave; every packet goes to
// the net log (net_log.hpp). The C++ twin of tools/emu/join_live.py.
//   net-join <save.d2s> <host> [--seconds N] [--game-exe PATH] [--log PATH]
// The tables come from your own game.exe (--game-exe, else $D2_GAME_EXE).
// Only ever point it at a host on your own network.
#include <d2gs/exe_tables.hpp>
#include <join.hpp>
#include <net_log.hpp>
#include <tcp.hpp>

#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
#include <ios>
#include <utility>

namespace {

auto now_ms() -> std::uint32_t {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

auto usage() -> int {
    std::fputs("usage: net-join <save.d2s> <host> [--seconds N] [--game-exe PATH] [--log PATH]\n", stderr);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.size() < 2) return usage();
    const std::string save_path(args[0]), host(args[1]);
    int seconds = 15;
    std::string game_exe = std::getenv("D2_GAME_EXE") ? std::getenv("D2_GAME_EXE") : "";
    std::string log_path = "net.log";
    for (std::size_t i = 2; i + 1 < args.size(); i += 2) {
        if (args[i] == "--seconds") seconds = std::atoi(std::string(args[i + 1]).c_str());
        else if (args[i] == "--game-exe") game_exe = args[i + 1];
        else if (args[i] == "--log") log_path = args[i + 1];
        else return usage();
    }
    if (game_exe.empty()) { std::fputs("net-join: no game.exe (--game-exe or D2_GAME_EXE)\n", stderr); return 2; }

    const auto tables = d2d::net::d2gs::load_exe_tables(game_exe);
    if (!tables) { std::fprintf(stderr, "net-join: %s\n", tables.error().c_str()); return 1; }
    std::ifstream save_file(save_path, std::ios::binary);
    std::vector<std::uint8_t> save{ std::istreambuf_iterator<char>(save_file), {} };
    auto session = d2d::net::JoinSession::create(*tables, std::move(save));
    if (!session) { std::fprintf(stderr, "net-join: %s\n", session.error().c_str()); return 1; }

    std::bitset<256> used;
    for (const int id : { 0x02, 0x04, 0x06, 0xaf, 0xb3, 0xb4 }) used.set(static_cast<std::size_t>(id));
    d2d::net::NetLog log(log_path, used);
    log.note("joining " + host + " as " + session->name());
    std::printf("%s joining %s as %s\n", d2d::net::utc_stamp().c_str(), host.c_str(), session->name().c_str());

    auto connection = d2d::net::TcpConnection::connect(host, 4000, 5000);
    if (!connection) { std::fprintf(stderr, "net-join: %s\n", connection.error().c_str()); return 1; }
    auto send = [&](const std::vector<d2d::net::Bytes>& packets) {
        for (const auto& packet : packets) {
            log.to_host(packet);
            if (auto sent = connection->send(packet); !sent) log.note("send failed: " + sent.error());
        }
    };

    auto state = session->state();
    std::uint32_t in_game_at = 0;
    while (now_ms() < static_cast<std::uint32_t>(seconds + 30) * 1000) {
        auto received = connection->receive(50);
        if (!received) { log.note(received.error()); break; }
        if (!received->empty()) {
            const auto step = session->receive(*received, now_ms());
            for (const auto& packet : step.packets) log.from_host(packet);
            send(step.to_send);
        }
        send(session->tick(now_ms()));
        if (session->state() != state) {
            state = session->state();
            log.note(std::string("state: ") + d2d::net::join_state_name(state));
            std::printf("%s state: %s (%.2f s)\n", d2d::net::utc_stamp().c_str(), d2d::net::join_state_name(state), now_ms() / 1000.0);
            if (state == d2d::net::JoinState::InGame) in_game_at = now_ms();
        }
        if (state == d2d::net::JoinState::InGame && now_ms() - in_game_at > static_cast<std::uint32_t>(seconds) * 1000) send(session->leave());
        if (state == d2d::net::JoinState::Refused) std::printf("refused: reason 0x%x\n", session->refused_reason());
        if (state == d2d::net::JoinState::Desync) std::printf("desync: %s\n", session->desync_reason().c_str());
        if (state == d2d::net::JoinState::Closed || state == d2d::net::JoinState::Refused || state == d2d::net::JoinState::Desync) break;
    }
    if (state == d2d::net::JoinState::Uploading) std::puts("the host never answered the join: is it in a game (not a menu)?");
    if (session->save_back_total()) std::printf("save-back: %zu / %u bytes (the tool doesn't write it; d2d does)\n", session->save_back().size(), session->save_back_total());
    std::fputs(log.summary().c_str(), stdout);
    std::printf("unknown S>C packets: %llu; log: %s\n", static_cast<unsigned long long>(log.unknown_count()), log_path.c_str());
    return state == d2d::net::JoinState::Closed ? 0 : 1;
}

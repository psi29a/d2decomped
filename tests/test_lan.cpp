// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: net game
// The LAN transport (components/net/lan.hpp): frames survive any cut of the
// byte stream, a host and clients on loopback greet, carry commands in
// order and views back, refuse a wrong version, and notice who leaves.
#include <lan.hpp>
#include <protocol.hpp>
#include <tcp.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <expected>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

using namespace d2d;
using net::lan::Bytes;

namespace {

// Polls the host and every client until `done` says so, or about 3 s pass.
template <class Done>
auto pump(net::lan::Host& host, std::vector<net::lan::Client*> clients, std::vector<net::lan::Host::Event>& host_events,
          std::vector<std::vector<net::lan::Client::Event>>& client_events, Done done) -> bool {
    client_events.resize(clients.size());
    for (int round = 0; round < 600; ++round) {
        for (auto& event : host.poll()) host_events.push_back(std::move(event));
        for (std::size_t index = 0; index < clients.size(); ++index)
            for (auto& event : clients[index]->poll()) client_events[index].push_back(std::move(event));
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

auto count(const std::vector<net::lan::Host::Event>& events, net::lan::Host::Event::Type type) -> int {
    int found = 0;
    for (const auto& event : events) found += event.type == type;
    return found;
}

}  // namespace

int main() try {
    // 1. Frames: one byte at a time, all at once, and two frames glued together all read back the same.
    {
        const Bytes payload{ 1, 2, 3, 250, 0, 7 };
        const auto first = net::lan::encode_frame(net::lan::Kind::Command, payload);
        const auto second = net::lan::encode_frame(net::lan::Kind::View, Bytes{});
        Bytes stream = first;
        stream.insert(stream.end(), second.begin(), second.end());
        net::lan::FrameReader trickle;
        for (const auto byte : stream) assert(trickle.feed(std::span(&byte, 1)));
        auto frame = trickle.next();
        assert(frame && frame->kind == net::lan::Kind::Command && frame->payload == payload);
        frame = trickle.next();
        assert(frame && frame->kind == net::lan::Kind::View && frame->payload.empty());
        assert(!trickle.next());
        net::lan::FrameReader whole;
        assert(whole.feed(stream));
        assert(whole.next() && whole.next() && !whole.next());
    }
    // 2. A bad stream is refused, not waited on.
    {
        net::lan::FrameReader oversized;
        const Bytes huge{ 0xff, 0xff, 0xff, 0x7f, 4 };
        assert(!oversized.feed(huge));
        net::lan::FrameReader unknown;
        const Bytes odd{ 0, 0, 0, 0, 99 };
        assert(!unknown.feed(odd));
    }
    // 3. The hello survives its wire form, and a short one is refused.
    {
        const net::lan::Hello hello{ 1, "Rogue", Bytes{ 0x55, 0xaa, 0x55, 0xaa } };
        const auto bytes = net::lan::encode_hello(hello);
        const auto back = net::lan::decode_hello(bytes);
        assert(back && back->version == 1 && back->name == "Rogue" && back->save == hello.save);
        assert(!net::lan::decode_hello(std::span(bytes).first(bytes.size() - 1)));
    }
    // 4. Loopback: three clients join, commands arrive in order, each gets its own view; a fourth is turned away.
    const auto port = std::uint16_t(46000 + std::random_device{}() % 2000);
    auto host = net::lan::Host::listen(port, "127.0.0.1");
    assert(host);
    std::vector<net::lan::Client> clients;
    for (const char* name : { "Red", "Blue", "Green" }) {
        auto client = net::lan::Client::join("127.0.0.1", { .name = name, .save = Bytes(900, 0x2a) }, port);
        assert(client);
        clients.push_back(std::move(*client));
    }
    std::vector<net::lan::Host::Event> host_events;
    std::vector<std::vector<net::lan::Client::Event>> client_events;
    std::vector<net::lan::Client*> all{ &clients[0], &clients[1], &clients[2] };
    assert(pump(*host, all, host_events, client_events, [&] { return count(host_events, net::lan::Host::Event::Type::Joined) == 3; }));
    assert(host->clients().size() == 3);
    for (const auto& event : host_events)
        if (event.type == net::lan::Host::Event::Type::Joined) assert(event.hello.save.size() == 900 && !event.hello.name.empty());

    constexpr int kCommands = 1000;
    for (int step = 0; step < kCommands; ++step)
        assert(clients[1].send_command(game::encode(game::cmd::Move{ float(step), float(step) * 0.5f, step % 2 == 0 })));
    assert(pump(*host, all, host_events, client_events, [&] { return count(host_events, net::lan::Host::Event::Type::Command) == kCommands; }));
    int expected = 0;
    for (const auto& event : host_events) {
        if (event.type != net::lan::Host::Event::Type::Command) continue;
        const auto command = game::decode(event.command);
        assert(command && std::holds_alternative<game::cmd::Move>(*command));
        assert(std::get<game::cmd::Move>(*command).x == float(expected));   // in order, none lost
        ++expected;
    }
    for (const int client : host->clients()) assert(host->send_view(client, Bytes(1500, std::uint8_t(client))));
    assert(pump(*host, all, host_events, client_events, [&] {
        int views = 0;
        for (const auto& events : client_events) for (const auto& event : events) views += event.type == net::lan::Client::Event::Type::View;
        return views == 3;
    }));
    for (const auto& events : client_events) {
        int player = -1;
        for (const auto& event : events) {
            if (event.type == net::lan::Client::Event::Type::Welcome) player = event.player;
            if (event.type == net::lan::Client::Event::Type::View) assert(event.view.size() == 1500 && event.view[0] == std::uint8_t(player));
        }
        assert(player > 0);   // 0 is the host's own player
    }
    {
        auto fourth = net::lan::Client::join("127.0.0.1", { .name = "Yellow" }, port);
        assert(fourth);
        std::vector<net::lan::Client*> just{ &*fourth };
        std::vector<std::vector<net::lan::Client::Event>> fourth_events;
        assert(pump(*host, just, host_events, fourth_events, [&] {
            return !fourth_events[0].empty() && fourth_events[0].back().type == net::lan::Client::Event::Type::Refused;
        }));
    }
    // 5. A wrong version is refused with a reason (a raw socket: Client::join always sends the right one).
    {
        clients[2].leave();   // make room, so the refusal is about the version
        assert(pump(*host, all, host_events, client_events, [&] { return host->clients().size() == 2; }));
        auto raw = net::TcpConnection::connect("127.0.0.1", port, 2000);
        assert(raw);
        const auto hello = net::lan::encode_hello({ .version = 99, .name = "Old" });
        assert(raw->send(net::lan::encode_frame(net::lan::Kind::Hello, hello)));
        net::lan::FrameReader reader;
        std::optional<net::lan::Frame> answer;
        for (int round = 0; round < 600 && !answer; ++round) {
            (void)host->poll();
            if (auto received = raw->receive(5); received && !received->empty()) { assert(reader.feed(*received)); answer = reader.next(); }
        }
        assert(answer && answer->kind == net::lan::Kind::Refuse);
        const std::string reason(answer->payload.begin(), answer->payload.end());
        assert(reason.find("protocol") != std::string::npos);
        assert(host->clients().size() == 2);
    }
    // 6. Leaving on purpose and dropping both show on the host.
    host_events.clear();
    clients[0].leave();
    { auto gone = std::move(clients[1]); }   // the socket closes without a goodbye
    std::vector<net::lan::Client*> none;
    assert(pump(*host, none, host_events, client_events, [&] { return count(host_events, net::lan::Host::Event::Type::Left) >= 2; }));
    // 9. A joiner that stops reading never holds the host up: its Views queue, then it's dropped.
    {
        auto stuck = net::TcpConnection::connect("127.0.0.1", port, 2000);
        assert(stuck);
        assert(stuck->send(net::lan::encode_frame(net::lan::Kind::Hello, net::lan::encode_hello({ .name = "Stuck" }))));
        host_events.clear();
        int number = -1;
        assert(pump(*host, none, host_events, client_events, [&] {
            for (const auto& event : host_events) if (event.type == net::lan::Host::Event::Type::Joined && event.hello.name == "Stuck") number = event.client;
            return number > 0;
        }));
        const Bytes view(64 * 1024, 0x11);
        std::chrono::milliseconds longest{ 0 };
        bool dropped = false;
        const auto started = std::chrono::steady_clock::now();
        while (!dropped && std::chrono::steady_clock::now() - started < std::chrono::seconds(8)) {
            const auto before = std::chrono::steady_clock::now();
            (void)host->send_view(number, view);
            for (auto& event : host->poll()) if (event.type == net::lan::Host::Event::Type::Left && event.client == number) dropped = true;
            longest = std::max(longest, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before));
        }
        assert(dropped);                     // "isn't reading" once kMaxPending is waiting, or timed out without a Pong
        assert(longest < std::chrono::milliseconds(500));
        assert(!host->send_view(number, view));
    }
    // 10. The host going away says Bye: a joiner learns the host left, not just that the socket closed.
    {
        auto last = net::lan::Client::join("127.0.0.1", { .name = "Last" }, port);
        assert(last);
        std::vector<net::lan::Client*> just{ &*last };
        std::vector<std::vector<net::lan::Client::Event>> last_events;
        host_events.clear();
        assert(pump(*host, just, host_events, last_events, [&] { return count(host_events, net::lan::Host::Event::Type::Joined) == 1; }));
        host = std::unexpected(std::string("gone"));
        std::string lost;
        for (int round = 0; round < 200 && lost.empty(); ++round) {
            for (const auto& event : last->poll()) if (event.type == net::lan::Client::Event::Type::Lost) lost = event.reason;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(lost == "the host left");
    }
    std::puts("lan transport: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_lan: %s\n", error.what());
    return 1;
}

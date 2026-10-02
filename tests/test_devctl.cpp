// SPDX-License-Identifier: GPL-3.0-or-later
// Unit + integration coverage for the dev control channel.
//   1. tokenize() edge cases (pure, cross-platform).
//   2. On POSIX, bind a real socket in $TMPDIR, connect via AF_UNIX, exchange
//      ping / custom / unknown verbs. Skipped on Windows (feature is POSIX-only).
#include <devctl.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>
#endif

int main() {
    using namespace d2d::devctl;

    // --- tokenize ---------------------------------------------------------
    {
        assert(tokenize("").empty());
        assert(tokenize("   \t \r\n").empty());

        auto tokens = tokenize("ping");
        assert(tokens.size() == 1 && tokens[0] == "ping");

        tokens = tokenize("screenshot /tmp/a.png");
        assert(tokens.size() == 2 && tokens[0] == "screenshot" && tokens[1] == "/tmp/a.png");

        tokens = tokenize("  key   Return   \n");
        assert(tokens.size() == 2 && tokens[0] == "key" && tokens[1] == "Return");
    }

#ifdef _WIN32
    (void)&Channel::pump;
    std::printf("OK (Windows: socket path skipped)\n");
    return 0;
#else
    // --- socket roundtrip -------------------------------------------------
    const char* tmp = std::getenv("TMPDIR");
    std::string sock_path = (tmp ? tmp : "/tmp");
    if (sock_path.back() != '/') sock_path.push_back('/');
    sock_path += "d2d-devctl-test." + std::to_string(::getpid()) + ".sock";

    Channel channel;
    channel.on("hello", [](const std::vector<std::string>& args) {
        return "greet " + (args.size() > 1 ? args[1] : std::string("world")) + "\n";
    });
    channel.on("boom", [](const std::vector<std::string>&) -> std::string {
        throw std::runtime_error("kablooey");
    });
    channel.listen(sock_path);
    assert(channel.active());

    // Client speaks the socket directly (no in-tree client to depend on).
    auto connect_client = [&] {
        int client = ::socket(AF_UNIX, SOCK_STREAM, 0);
        assert(client >= 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, sock_path.c_str(), sizeof(address.sun_path) - 1);
        int result = ::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        assert(result == 0);
        return client;
    };

    auto read_reply = [](int socket_fd) {
        std::string acc;
        for (int i = 0; i < 200; ++i) {
            char buf[512];
            ssize_t received = ::recv(socket_fd, buf, sizeof(buf), 0);
            if (received > 0) { acc.append(buf, std::size_t(received)); if (acc.find('\n') != std::string::npos) break; }
            else if (received == 0) break;
            else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            } else break;
        }
        return acc;
    };

    auto tick = [&](int rounds = 20) {
        for (int i = 0; i < rounds; ++i) {
            channel.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };

    // Ping (built-in).
    int client1 = connect_client();
    tick();
    ::send(client1, "ping\n", 5, 0);
    tick();
    auto reply = read_reply(client1);
    assert(reply == "ok\n");

    // Custom handler with args.
    ::send(client1, "hello there\n", 12, 0);
    tick();
    reply = read_reply(client1);
    assert(reply == "greet there\n");

    // Unknown verb.
    ::send(client1, "does-not-exist\n", 15, 0);
    tick();
    reply = read_reply(client1);
    assert(reply.starts_with("err unknown verb"));

    // Thrown exception → err message.
    ::send(client1, "boom\n", 5, 0);
    tick();
    reply = read_reply(client1);
    assert(reply == "err kablooey\n");

    // Second concurrent client gets "err busy" and is dropped.
    int client2 = connect_client();
    tick();
    reply = read_reply(client2);
    assert(reply == "err busy\n");
    ::close(client2);

    ::close(client1);
    tick();

    // Regression: a client that sends a batch of commands then SHUT_WRs
    // must still get every reply — the pump had a bug where EOF on recv
    // closed the fd before dispatching buffered lines. Repro with a fresh
    // client that half-closes right after sending.
    int client3 = connect_client();
    ::send(client3, "hello alice\nhello bob\nping\n", 27, 0);
    ::shutdown(client3, SHUT_WR);
    tick();
    reply = read_reply(client3);
    assert(reply == "greet alice\ngreet bob\nok\n");
    ::close(client3);
    tick();

    channel.close();
    assert(!channel.active());

    std::printf("OK\n");
    return 0;
#endif
}

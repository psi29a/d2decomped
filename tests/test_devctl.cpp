// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: devctl
// Unit + integration coverage for the dev control channel.
//   1. tokenize() edge cases (pure, cross-platform).
//   2. Bind a real socket in the temp dir, connect via AF_UNIX (Winsock's on
//      Windows 10 1803+), exchange ping / custom / unknown verbs.
#include <devctl.hpp>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>   // first: afunix.h needs its types

#include <afunix.h>
#include <process.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

// The test's own client calls, Winsock or POSIX.
#ifdef _WIN32
using Native = SOCKET;
constexpr Native kNone = INVALID_SOCKET;
void close_native(Native socket) { ::closesocket(socket); }
bool would_block() { return ::WSAGetLastError() == WSAEWOULDBLOCK; }
long long send_text(Native socket, std::string_view text) { return ::send(socket, text.data(), int(text.size()), 0); }
long long recv_some(Native socket, char* data, std::size_t size) { return ::recv(socket, data, int(size), 0); }
void shut_write(Native socket) { ::shutdown(socket, SD_SEND); }
int process_id() { return ::_getpid(); }
bool set_nonblocking(Native socket) { u_long enabled = 1; return ::ioctlsocket(socket, FIONBIO, &enabled) == 0; }
#else
using Native = int;
constexpr Native kNone = -1;
void close_native(Native socket) { ::close(socket); }
bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK; }
long long send_text(Native socket, std::string_view text) { return ::send(socket, text.data(), text.size(), 0); }
long long recv_some(Native socket, char* data, std::size_t size) { return ::recv(socket, data, size, 0); }
void shut_write(Native socket) { ::shutdown(socket, SHUT_WR); }
int process_id() { return int(::getpid()); }
bool set_nonblocking(Native socket) { const int flags = ::fcntl(socket, F_GETFL, 0); return flags >= 0 && ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0; }
#endif

}  // namespace

int main() try {
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

    // --- socket roundtrip -------------------------------------------------
    const std::string sock_path = (std::filesystem::temp_directory_path() / ("d2d-devctl-test." + std::to_string(process_id()) + ".sock")).string();

    Channel channel;
    channel.on("hello", [](const std::vector<std::string>& args) {
        return "greet " + (args.size() > 1 ? args[1] : std::string("world")) + "\n";
    });
    channel.on("boom", [](const std::vector<std::string>&) -> std::string {
        throw std::runtime_error("kablooey");
    });
    channel.listen(sock_path);   // starts Winsock too, before the client's first call
    assert(channel.active());

    // Client speaks the socket directly (no in-tree client to depend on).
    auto connect_client = [&] {
        const Native client = ::socket(AF_UNIX, SOCK_STREAM, 0);
        assert(client != kNone);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, sock_path.c_str(), sizeof(address.sun_path) - 1);
        const int result = ::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        assert(result == 0);
        // Non-blocking: read_reply's retries bound the wait (~1 s) if a reply never comes.
        const bool nonblocking = set_nonblocking(client);
        assert(nonblocking);
        return client;
    };

    auto read_reply = [](Native socket_fd) {
        std::string acc;
        for (int i = 0; i < 200; ++i) {
            char buf[512];
            const long long received = recv_some(socket_fd, buf, sizeof(buf));
            if (received > 0) { acc.append(buf, std::size_t(received)); if (acc.find('\n') != std::string::npos) break; }
            else if (received == 0) break;
            else if (would_block()) {
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
    const Native client1 = connect_client();
    tick();
    assert(send_text(client1, "ping\n") == 5);
    tick();
    auto reply = read_reply(client1);
    assert(reply == "ok\n");

    // Custom handler with args.
    assert(send_text(client1, "hello there\n") == 12);
    tick();
    reply = read_reply(client1);
    assert(reply == "greet there\n");

    // Unknown verb.
    assert(send_text(client1, "does-not-exist\n") == 15);
    tick();
    reply = read_reply(client1);
    assert(reply.starts_with("err unknown verb"));

    // Thrown exception → err message.
    assert(send_text(client1, "boom\n") == 5);
    tick();
    reply = read_reply(client1);
    assert(reply == "err kablooey\n");

    // Second concurrent client gets "err busy" and is dropped.
    const Native client2 = connect_client();
    tick();
    reply = read_reply(client2);
    assert(reply == "err busy\n");
    close_native(client2);

    close_native(client1);
    tick();

    // Regression: a client that sends a batch of commands then SHUT_WRs
    // must still get every reply — the pump had a bug where EOF on recv
    // closed the fd before dispatching buffered lines. Repro with a fresh
    // client that half-closes right after sending.
    const Native client3 = connect_client();
    assert(send_text(client3, "hello alice\nhello bob\nping\n") == 27);
    shut_write(client3);
    tick();
    reply = read_reply(client3);
    assert(reply == "greet alice\ngreet bob\nok\n");
    close_native(client3);
    tick();

    channel.close();
    assert(!channel.active());
    assert(!std::filesystem::exists(sock_path));   // close() removes the socket file

    std::printf("OK\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_devctl: %s\n", error.what());
    return 1;
}

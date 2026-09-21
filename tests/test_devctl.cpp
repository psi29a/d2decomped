// Unit + integration coverage for the dev control channel.
//   1. tokenize() edge cases (pure, cross-platform).
//   2. On POSIX, bind a real socket in $TMPDIR, connect via AF_UNIX, exchange
//      ping / custom / unknown verbs. Skipped on Windows (feature is POSIX-only).
#include <devctl.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
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

        auto t = tokenize("ping");
        assert(t.size() == 1 && t[0] == "ping");

        t = tokenize("screenshot /tmp/a.png");
        assert(t.size() == 2 && t[0] == "screenshot" && t[1] == "/tmp/a.png");

        t = tokenize("  key   Return   \n");
        assert(t.size() == 2 && t[0] == "key" && t[1] == "Return");
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

    Channel ch;
    ch.on("hello", [](const std::vector<std::string>& args) {
        return "greet " + (args.size() > 1 ? args[1] : std::string("world")) + "\n";
    });
    ch.on("boom", [](const std::vector<std::string>&) -> std::string {
        throw std::runtime_error("kablooey");
    });
    ch.listen(sock_path);
    assert(ch.active());

    // Client speaks the socket directly (no in-tree client to depend on).
    auto connect_client = [&] {
        int c = ::socket(AF_UNIX, SOCK_STREAM, 0);
        assert(c >= 0);
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        std::strncpy(a.sun_path, sock_path.c_str(), sizeof(a.sun_path) - 1);
        int rc = ::connect(c, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        assert(rc == 0);
        return c;
    };

    auto read_reply = [](int fd) {
        std::string acc;
        for (int i = 0; i < 200; ++i) {
            char buf[512];
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n > 0) { acc.append(buf, std::size_t(n)); if (acc.find('\n') != std::string::npos) break; }
            else if (n == 0) break;
            else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            } else break;
        }
        return acc;
    };

    auto tick = [&](int n = 20) {
        for (int i = 0; i < n; ++i) {
            ch.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };

    // Ping (built-in).
    int c1 = connect_client();
    tick();
    ::send(c1, "ping\n", 5, 0);
    tick();
    auto reply = read_reply(c1);
    assert(reply == "ok\n");

    // Custom handler with args.
    ::send(c1, "hello there\n", 12, 0);
    tick();
    reply = read_reply(c1);
    assert(reply == "greet there\n");

    // Unknown verb.
    ::send(c1, "does-not-exist\n", 15, 0);
    tick();
    reply = read_reply(c1);
    assert(reply.starts_with("err unknown verb"));

    // Thrown exception → err message.
    ::send(c1, "boom\n", 5, 0);
    tick();
    reply = read_reply(c1);
    assert(reply == "err kablooey\n");

    // Second concurrent client gets "err busy" and is dropped.
    int c2 = connect_client();
    tick();
    reply = read_reply(c2);
    assert(reply == "err busy\n");
    ::close(c2);

    ::close(c1);
    tick();

    // Regression: a client that sends a batch of commands then SHUT_WRs
    // must still get every reply — the pump had a bug where EOF on recv
    // closed the fd before dispatching buffered lines. Repro with a fresh
    // client that half-closes right after sending.
    int c3 = connect_client();
    ::send(c3, "hello alice\nhello bob\nping\n", 27, 0);
    ::shutdown(c3, SHUT_WR);
    tick();
    reply = read_reply(c3);
    assert(reply == "greet alice\ngreet bob\nok\n");
    ::close(c3);
    tick();

    ch.close();
    assert(!ch.active());

    std::printf("OK\n");
    return 0;
#endif
}

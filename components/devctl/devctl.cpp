// SPDX-License-Identifier: GPL-3.0-or-later
#include "devctl.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>   // first: afunix.h needs its types

#include <afunix.h>
#include <filesystem>
#include <system_error>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#if !defined(__vita__) && !defined(__SWITCH__)   // no Unix sockets there: listen() says so and stays off
#include <sys/un.h>
#endif
#include <unistd.h>
#endif

namespace d2d::devctl {

// tokenize() is pure and platform-neutral.
std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> out;
    std::size_t offset = 0;
    while (offset < line.size()) {
        while (offset < line.size() && (line[offset] == ' ' || line[offset] == '\t' ||
                                   line[offset] == '\r' || line[offset] == '\n')) ++offset;
        const auto start = offset;
        while (offset < line.size() && line[offset] != ' ' && line[offset] != '\t' &&
               line[offset] != '\r' && line[offset] != '\n') ++offset;
        if (start < offset) out.emplace_back(line.substr(start, offset - start));
    }
    return out;
}

namespace {

// The few calls that differ between Winsock (AF_UNIX since Windows 10 1803)
// and POSIX sockets; the channel above them is the same on both.
#ifdef _WIN32
using Native = SOCKET;
constexpr Native kNone = INVALID_SOCKET;
auto close_native(Native socket) -> void { ::closesocket(socket); }
auto last_error() -> std::string { return "winsock error " + std::to_string(::WSAGetLastError()); }
auto ensure_started() -> bool {
    static const bool started = [] { WSADATA data{}; return ::WSAStartup(MAKEWORD(2, 2), &data) == 0; }();
    return started;
}
auto set_nonblocking(Native socket) -> bool { u_long enabled = 1; return ::ioctlsocket(socket, FIONBIO, &enabled) == 0; }
auto would_block() -> bool { return ::WSAGetLastError() == WSAEWOULDBLOCK; }
auto interrupted() -> bool { return ::WSAGetLastError() == WSAEINTR; }
auto wait_writable(Native socket, int timeout_ms) -> bool { WSAPOLLFD entry{ socket, POLLOUT, 0 }; return ::WSAPoll(&entry, 1, timeout_ms) > 0; }
auto send_some(Native socket, const char* data, std::size_t size) -> long long { return ::send(socket, data, int(size), 0); }
auto recv_some(Native socket, char* data, std::size_t size, int flags) -> long long { return ::recv(socket, data, int(size), flags); }
auto remove_path(const std::string& path) -> void { std::error_code ignored; std::filesystem::remove(path, ignored); }
auto ignore_sigpipe() -> void {}                              // Winsock has no SIGPIPE
#else
using Native = int;
constexpr Native kNone = -1;
auto close_native(Native socket) -> void { ::close(socket); }
// maybe_unused: only listen() calls these two, and the Vita's does nothing.
[[maybe_unused]] auto last_error() -> std::string { return std::strerror(errno); }
[[maybe_unused]] auto ensure_started() -> bool { return true; }
auto set_nonblocking(Native socket) -> bool { const int flags = ::fcntl(socket, F_GETFL, 0); return flags >= 0 && ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0; }
auto would_block() -> bool { return errno == EAGAIN || errno == EWOULDBLOCK; }
auto interrupted() -> bool { return errno == EINTR; }
auto wait_writable(Native socket, int timeout_ms) -> bool { pollfd entry{ socket, POLLOUT, 0 }; return ::poll(&entry, 1, timeout_ms) > 0; }
auto send_some(Native socket, const char* data, std::size_t size) -> long long { return ::send(socket, data, size, 0); }
auto recv_some(Native socket, char* data, std::size_t size, int flags) -> long long { return ::recv(socket, data, size, flags); }
auto remove_path(const std::string& path) -> void { ::unlink(path.c_str()); }
// A client disconnect mid-send can raise SIGPIPE on macOS pre-10.2. We
// ignore it once, globally — same shape as thirdeye.
auto ignore_sigpipe() -> void { std::signal(SIGPIPE, SIG_IGN); }
#endif

void write_all(Native socket, std::string_view text) {
    const char* cursor = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
        const long long sent = send_some(socket, cursor, remaining);
        if (sent > 0) { cursor += sent; remaining -= std::size_t(sent); continue; }
        if (sent < 0 && interrupted()) continue;
        // Non-blocking socket, buffer full (a big reply): wait for it to
        // drain rather than cut the reply short. 2 s without progress:
        // treat the client as stuck.
        if (sent < 0 && would_block() && wait_writable(socket, 2000)) continue;
        break;   // client gone; caller will notice on next pump
    }
}

}  // namespace

struct Channel::Impl {
    Native       listen_fd = kNone;
    Native       client_fd = kNone;
    std::string  rx_buf;
    std::string  path;
    std::unordered_map<std::string, Handler> verbs;

    void close_client() {
        if (client_fd != kNone) {
            close_native(client_fd);
            client_fd = kNone;
        }
        rx_buf.clear();
    }
};

Channel::Channel() : impl_(new Impl) {
    // Default verb: ping.
    impl_->verbs.emplace(std::string("ping"),
        [](const std::vector<std::string>&) -> std::string { return "ok\n"; });
}

Channel::~Channel() {
    close();
    delete impl_;
}

void Channel::on(std::string verb, Handler handler) {
    impl_->verbs[std::move(verb)] = std::move(handler);
}

bool Channel::active() const noexcept { return impl_->listen_fd != kNone; }

void Channel::listen(const std::string& path) {
    if (path.empty()) return;
    if (impl_->listen_fd != kNone) close();
    ignore_sigpipe();
#if defined(__vita__) || defined(__SWITCH__)
    std::fprintf(stderr, "[devctl] no Unix sockets here: %s not opened\n", path.c_str());
#else
    if (path.size() >= sizeof(sockaddr_un{}.sun_path)) {
        std::fprintf(stderr, "[devctl] path too long: %s\n", path.c_str());
        return;
    }
    if (!ensure_started()) {
        std::fprintf(stderr, "[devctl] startup: %s\n", last_error().c_str());
        return;
    }
    const Native socket_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == kNone) {
        std::fprintf(stderr, "[devctl] socket: %s\n", last_error().c_str());
        return;
    }
    remove_path(path);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "[devctl] bind %s: %s\n", path.c_str(), last_error().c_str());
        close_native(socket_fd);
        return;
    }
    if (::listen(socket_fd, 1) != 0) {
        std::fprintf(stderr, "[devctl] listen: %s\n", last_error().c_str());
        close_native(socket_fd);
        remove_path(path);
        return;
    }
    if (!set_nonblocking(socket_fd)) {
        std::fprintf(stderr, "[devctl] nonblocking: %s\n", last_error().c_str());
        close_native(socket_fd);
        remove_path(path);
        return;
    }
    impl_->listen_fd = socket_fd;
    impl_->path = path;
    std::fprintf(stderr, "[devctl] listening on %s\n", path.c_str());
#endif
}

void Channel::pump() {
    if (impl_->listen_fd == kNone) return;

    // Drain existing client BEFORE accept: a fast connect/close/connect
    // sequence would otherwise look like an overlap and the new client would
    // get "err busy" while the old fd hadn't reported EOF yet.
    if (impl_->client_fd != kNone) {
        char buf[512];
        bool remote_eof = false;
        while (true) {
            const long long received = recv_some(impl_->client_fd, buf, sizeof(buf), 0);
            if (received > 0) { impl_->rx_buf.append(buf, std::size_t(received)); continue; }
            if (received == 0) { remote_eof = true; break; }
            if (would_block()) break;
            if (interrupted()) continue;
            impl_->close_client();
            break;
        }
        // Dispatch every complete line we have — even if the peer half-closed
        // (SHUT_WR) after sending a batch, they still expect replies before
        // the server tears the fd down.
        std::size_t newline;
        while (impl_->client_fd != kNone &&
               (newline = impl_->rx_buf.find('\n')) != std::string::npos) {
            std::string line = impl_->rx_buf.substr(0, newline);
            impl_->rx_buf.erase(0, newline + 1);
            auto tok = tokenize(line);
            std::string reply;
            if (tok.empty()) {
                reply = "err empty\n";
            } else {
                auto found = impl_->verbs.find(tok[0]);
                if (found == impl_->verbs.end()) {
                    reply = "err unknown verb '" + tok[0] + "'\n";
                } else {
                    try {
                        reply = found->second(tok);
                    } catch (const std::exception& error) {
                        reply = std::string("err ") + error.what() + "\n";
                    } catch (...) {
                        reply = "err unknown exception\n";
                    }
                    if (reply.empty() || reply.back() != '\n') reply += '\n';
                }
            }
            write_all(impl_->client_fd, reply);
        }
        // 64 KB is generous for a text protocol; anything larger is a bug.
        if (impl_->client_fd != kNone && impl_->rx_buf.size() > 64 * 1024) {
            write_all(impl_->client_fd, "err line too long\n");
            impl_->close_client();
        }
        // Only tear the fd down AFTER the buffered lines have been dispatched
        // and their replies flushed — the client already stopped writing.
        if (remote_eof && impl_->client_fd != kNone) {
            impl_->close_client();
        }
    }

    // Accept new clients now that the previous one has been fully drained.
    while (true) {
        const Native client = ::accept(impl_->listen_fd, nullptr, nullptr);
        if (client == kNone) break;
        if (!set_nonblocking(client)) { close_native(client); continue; }
        // The old client may have read its reply and hung up since the
        // drain above (a fast poller reconnects within one pump).
        if (char peek; impl_->client_fd != kNone && impl_->rx_buf.empty() && recv_some(impl_->client_fd, &peek, 1, MSG_PEEK) == 0)
            impl_->close_client();
        if (impl_->client_fd != kNone) {
            const char busy[] = "err busy\n";
            (void)send_some(client, busy, sizeof(busy) - 1);
            close_native(client);
        } else {
            impl_->client_fd = client;
        }
    }
}

void Channel::close() {
    impl_->close_client();
    if (impl_->listen_fd != kNone) {
        close_native(impl_->listen_fd);
        impl_->listen_fd = kNone;
    }
    if (!impl_->path.empty()) {
        remove_path(impl_->path);
        impl_->path.clear();
    }
}

}  // namespace d2d::devctl

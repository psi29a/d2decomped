#include "devctl.hpp"

#include <cstdio>
#include <exception>
#include <unordered_map>

namespace d2d::devctl {

// tokenize() is pure and platform-neutral — kept out of the POSIX gate.
std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' ||
                                   line[i] == '\r' || line[i] == '\n')) ++i;
        const auto start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' &&
               line[i] != '\r' && line[i] != '\n') ++i;
        if (start < i) out.emplace_back(line.substr(start, i - start));
    }
    return out;
}

}  // namespace d2d::devctl

#ifdef _WIN32
// Windows: no AF_UNIX; keep call sites portable with a warn-once stub.
namespace d2d::devctl {

struct Channel::Impl { bool warned = false; };

Channel::Channel()  : impl_(new Impl) {}
Channel::~Channel() { delete impl_; }

void Channel::listen(const std::string& path) {
    if (!path.empty() && !impl_->warned) {
        std::fprintf(stderr, "[devctl] not supported on Windows (path=%s)\n",
                     path.c_str());
        impl_->warned = true;
    }
}
void Channel::on(std::string, Handler) {}
void Channel::pump() {}
void Channel::close() {}
bool Channel::active() const noexcept { return false; }

}  // namespace d2d::devctl

#else  // POSIX

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace d2d::devctl {

struct Channel::Impl {
    int          listen_fd = -1;
    int          client_fd = -1;
    std::string  rx_buf;
    std::string  path;
    std::unordered_map<std::string, Handler> verbs;

    void close_client() {
        if (client_fd >= 0) {
            ::close(client_fd);
            client_fd = -1;
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

void Channel::on(std::string verb, Handler h) {
    impl_->verbs[std::move(verb)] = std::move(h);
}

bool Channel::active() const noexcept { return impl_->listen_fd >= 0; }

static void write_all(int fd, std::string_view s) {
    const char* p = s.data();
    std::size_t n = s.size();
    while (n > 0) {
        ssize_t w = ::send(fd, p, n, 0);
        if (w > 0) { p += w; n -= std::size_t(w); continue; }
        if (w < 0 && errno == EINTR) continue;
        break;   // client gone; caller will notice on next pump
    }
}

void Channel::listen(const std::string& path) {
    if (path.empty()) return;
    if (impl_->listen_fd >= 0) close();

    // A client disconnect mid-send can raise SIGPIPE on macOS pre-10.2. We
    // ignore it once, globally — same shape as thirdeye.
    std::signal(SIGPIPE, SIG_IGN);

    if (path.size() >= sizeof(sockaddr_un{}.sun_path)) {
        std::fprintf(stderr, "[devctl] path too long: %s\n", path.c_str());
        return;
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "[devctl] socket: %s\n", std::strerror(errno));
        return;
    }
    ::unlink(path.c_str());
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "[devctl] bind %s: %s\n",
                     path.c_str(), std::strerror(errno));
        ::close(fd);
        return;
    }
    if (::listen(fd, 1) < 0) {
        std::fprintf(stderr, "[devctl] listen: %s\n", std::strerror(errno));
        ::close(fd);
        ::unlink(path.c_str());
        return;
    }
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    impl_->listen_fd = fd;
    impl_->path = path;
    std::fprintf(stderr, "[devctl] listening on %s\n", path.c_str());
}

void Channel::pump() {
    if (impl_->listen_fd < 0) return;

    // Drain existing client BEFORE accept: a fast connect/close/connect
    // sequence would otherwise look like an overlap and the new client would
    // get "err busy" while the old fd hadn't reported EOF yet.
    if (impl_->client_fd >= 0) {
        char buf[512];
        bool remote_eof = false;
        while (true) {
            ssize_t n = ::recv(impl_->client_fd, buf, sizeof(buf), 0);
            if (n > 0) { impl_->rx_buf.append(buf, std::size_t(n)); continue; }
            if (n == 0) { remote_eof = true; break; }
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            impl_->close_client();
            break;
        }
        // Dispatch every complete line we have — even if the peer half-closed
        // (SHUT_WR) after sending a batch, they still expect replies before
        // the server tears the fd down.
        std::size_t nl;
        while (impl_->client_fd >= 0 &&
               (nl = impl_->rx_buf.find('\n')) != std::string::npos) {
            std::string line = impl_->rx_buf.substr(0, nl);
            impl_->rx_buf.erase(0, nl + 1);
            auto tok = tokenize(line);
            std::string reply;
            if (tok.empty()) {
                reply = "err empty\n";
            } else {
                auto it = impl_->verbs.find(tok[0]);
                if (it == impl_->verbs.end()) {
                    reply = "err unknown verb '" + tok[0] + "'\n";
                } else {
                    try {
                        reply = it->second(tok);
                    } catch (const std::exception& e) {
                        reply = std::string("err ") + e.what() + "\n";
                    } catch (...) {
                        reply = "err unknown exception\n";
                    }
                    if (reply.empty() || reply.back() != '\n') reply += '\n';
                }
            }
            write_all(impl_->client_fd, reply);
        }
        // 64 KB is generous for a text protocol; anything larger is a bug.
        if (impl_->client_fd >= 0 && impl_->rx_buf.size() > 64 * 1024) {
            write_all(impl_->client_fd, "err line too long\n");
            impl_->close_client();
        }
        // Only tear the fd down AFTER the buffered lines have been dispatched
        // and their replies flushed — the client already stopped writing.
        if (remote_eof && impl_->client_fd >= 0) {
            impl_->close_client();
        }
    }

    // Accept new clients now that the previous one has been fully drained.
    while (true) {
        int c = ::accept(impl_->listen_fd, nullptr, nullptr);
        if (c < 0) break;
        const int flags = ::fcntl(c, F_GETFL, 0);
        ::fcntl(c, F_SETFL, flags | O_NONBLOCK);
        if (impl_->client_fd >= 0) {
            const char busy[] = "err busy\n";
            (void)::send(c, busy, sizeof(busy) - 1, 0);
            ::close(c);
        } else {
            impl_->client_fd = c;
        }
    }
}

void Channel::close() {
    impl_->close_client();
    if (impl_->listen_fd >= 0) {
        ::close(impl_->listen_fd);
        impl_->listen_fd = -1;
    }
    if (!impl_->path.empty()) {
        ::unlink(impl_->path.c_str());
        impl_->path.clear();
    }
}

}  // namespace d2d::devctl

#endif  // _WIN32

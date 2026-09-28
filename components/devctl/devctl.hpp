// D2Decomp dev control channel — Unix domain socket for local development.
//
// Same shape as third-eye's THIRDEYE::control (docs/control_channel.md over
// there): line-based text protocol, single client at a time, non-blocking
// pump() called once per tick from the main loop. Zero cost when listen()
// isn't called.
//
// The channel is engine-agnostic: register verb handlers with on(), and
// handlers return the reply text (must end with '\n', usually "ok\n" or
// "err …\n"). One built-in verb: `ping` → `ok\n`.
//
// Local dev only. There is no auth and the socket is on the local
// filesystem — never enable in shipped builds.
//
// POSIX only. On Windows, listen() is a warn-and-noop and pump() does
// nothing, so callers stay portable.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::devctl {

// Verb handler: receives the tokenised argument list (including the verb
// itself as tokens[0]) and returns the reply written back to the client.
// Reply must end with '\n'. Throw for internal errors — the pump catches
// and sends "err <what>\n".
using Handler = std::function<std::string(const std::vector<std::string>&)>;

class Channel {
public:
    Channel();
    ~Channel();

    Channel(const Channel&)            = delete;
    Channel& operator=(const Channel&) = delete;

    // Bind AF_UNIX socket at `path` (unlinking any stale file first). Empty
    // path is a no-op — call unconditionally, gate at the caller.
    void listen(const std::string& path);

    // Register a verb handler. Overwrites any existing handler for that verb.
    void on(std::string verb, Handler handler);

    // Poll: accept new clients (single client at a time; extra get
    // `err busy\n`), drain the connected client, dispatch complete lines.
    void pump();

    // Close socket, unlink file. Safe to call multiple times.
    void close();

    // True once listen() successfully bound.
    [[nodiscard]] bool active() const noexcept;

private:
    struct Impl;
    Impl* impl_;
};

// Pure: whitespace-split a single line. Public for tests.
std::vector<std::string> tokenize(std::string_view line);

}  // namespace d2d::devctl

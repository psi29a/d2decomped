# Dev control channel

Local-development only. Speaks over an AF_UNIX stream socket at a path
chosen by the host binary. Line-based text protocol, single client at a
time, non-blocking pump driven from the game's main loop.

**Never enable in shipped builds.** There is no auth, and any local
process that can `connect()` the socket can drive input, dump state, and
in principle write files (via `screenshot`).

## Transport

- Framing: LF-terminated (`\n`) lines, one command per line.
- Encoding: ASCII/UTF-8; whitespace-separated tokens.
- Reply: LF-terminated. Success ends with `ok\n` (either as the sole reply
  or after any payload lines). Failure is `err <message>\n`.
- Second concurrent client is rejected with `err busy\n` and dropped.
- A line longer than 64 KiB drops the client with `err line too long\n`.

## Built-in verbs

| verb            | reply             | notes                                                  |
|-----------------|-------------------|--------------------------------------------------------|
| `ping`          | `ok`              | Liveness probe.                                        |
| *unknown verb*  | `err unknown verb '<name>'` | Handler not registered.                     |

Everything else is registered by the host binary via `Channel::on()`.

## `apps/d2d/` verbs (current)

| verb                        | reply                  | notes                                                     |
|-----------------------------|------------------------|-----------------------------------------------------------|
| `screenshot <path>`         | `ok <bytes>`           | Writes current framebuffer to `<path>` as PNG. Relative paths land in `<user dir>/screenshots/` (e.g. `~/Library/Preferences/d2d/screenshots/`). |
| `info`                      | `w=… h=… frame=…\nok`  | Framebuffer dimensions + current frame counter.           |
| `quit`                      | `ok`                   | Main loop exits after this pump completes.                |
| `click <x> <y>`             | `ok`                   | Pushes real SDL motion + left down/up at window coords; handled next frame. |
| `wheel <dy>`                | `ok`                   | Pushes an SDL wheel event (+up / -down); handled next frame. |
| `state`                     | `screen=… save=… scroll=… class=… name=… hardcore=… cam=x,y saves=…\nok` | Loop state for scripted asserts. |

Future verbs (as the engine grows): `key <name>`, `load <act>/<level>`, etc.

## Enabling

Any host that links `devctl` calls `Channel::listen(path)` — an empty path
is a no-op, so a `--devctl <path>` CLI flag or `D2D_DEVCTL` env var gates
it. Absent that, zero cost — nothing binds, no threads, no cleanup.

## Driving from the shell

```
# Start the game with control enabled (add --headless for no window —
# SDL dummy driver + software renderer; tests/smoke_d2d.py drives it):
d2d --devctl /tmp/d2d.sock

# Drive it from another shell:
nc -U /tmp/d2d.sock <<'EOF'
ping
info
screenshot /tmp/frame.png
quit
EOF
```

`nc -U` speaks AF_UNIX. On macOS `nc` is BSD `netcat`; on Linux use
`ncat -U` from nmap-ncat if the packaged `nc` is missing.

## Prior art

Same shape as the third-eye control channel
(`/Users/bret.curtis/Workspace/private/eob3/thirdeye/apps/thirdeye/control.cpp`).
The differences are:

- Handler registration is decoupled — devctl doesn't know about game types.
- Reply framing enforced by the pump (auto-appends `\n` if missing).
- No auto-release ticks (mouse-button state is engine-side when we get there).

## POSIX only

Windows path is a warn-once stub. Call sites stay portable — the header
compiles everywhere; `listen()` on Windows just prints a warning and
`pump()` becomes a no-op.

# LAN host tests (docs/design/lan-host.md)

Scripted headless runs on one machine over 127.0.0.1, not in ctest: they need
the game files (`D2_MPQ_DIR`) and a d2d build (`D2D`, default
`build/apps/d2d/d2d`). Each prints ok / FAIL lines.

- `lan_split.py`: players on their own levels, chat, deaths, joins and leaves.
- `lan_soak.py MINUTES [JOINERS] [netem]`: a soak.

Environment the scripts and d2d read:

- `D2D_LAN_SAVE_SECONDS`: how often the host sends a joiner its save (5 minutes without it).
- `D2D_LAN_BAD_VIEW=n`: a joiner cuts View n in half, as a bug would send it (the Resync path).
- `D2D_LAN_SOUNDS`: a joiner logs how many sounds each View brought.

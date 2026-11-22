# Council S1: the scheduled animation event

First traced on 2026-11-01 against a locally owned game.exe 1.14d (file
version 1.14.3.71). The timing comparison covers the ordinary S1 shot
and mode end.

## Scope and result

The original scheduler (`FUN_005539b0`) files one action event, type 0
with animation event value 2 and action index 0, on the first tick whose
animation position reaches the event frame: tick max(1, ceil(256 x 3 /
rate)), where rate is the word `FUN_00623f50` stores at unit +0x4c.
Dispatching that event through the original mode dispatcher reaches the S1
missile request.

The local MonStats rows for councilmember1, councilmember2 and
councilmember3 use code HP, AI HighPriest and MissS1
`highpriestlightning`. The local AnimData record HPS1HTH has 14 frames,
rate 200 and its only event, value 2, at frame index 3. That 200 is
**not** the rate S1 plays at: game.exe's mode row for S1 at `0x6e8b90`
(classes under 0x19a, `+0x14 x 8 + 4` = 1) makes it a moving mode for
`FUN_006214a0`, so `FUN_00623f50` stores the MonStats walk base times
velocitypercent (stat 0x43), as for WL. With the stock velocitypercent
75 (`FUN_00573cb0`) the rates are 126, 168 and 210, and the shot comes
**7, 5 and 4 ticks** after the mode starts for classes 345, 346 and 347.
The first section's fixtures write 200 at +0x4c, where the event is at
start + 4 ticks.

d2d files its shot on the same tick (`ai.hpp` `action_due_ms`:
`rules::frame_tick` on `mode_rate`), checked against game.exe over walk %
and anim % below. The old ms deadline gave the same ticks over that grid;
it differed elsewhere (at a 254 rate for this record, tick 3 against 4:
`tests/game/test_action_tick.cpp`).

## Original-code route

| Address | Evidence |
|---|---|
| `FUN_005e0490` | HighPriest AI selects mode 8 through `FUN_005ddf90`; this is not a direct missile call. |
| `FUN_005a7c20` | The mode-change path calls the mode's start callback, then schedules stationary-mode events through `FUN_005539b0`. |
| `FUN_005a78a0` | Selects the ordinary mode callback record for Council classes 345..347. Mode 8's record at `0x006e22e0` has start `0x005a75c0`, action `0x005a7670`, end `0x005a8030`. |
| `FUN_005a75c0` | Starts the mode. With no active skill entry it does not call the missile-creation path. |
| `FUN_005539b0` | Advances fixed-point animation positions and files type-0 events for markers 1, 2 and 4 through `FUN_005417d0`. |
| `FUN_005a7ba0` | Type-0 event callback, selected by `0x006e2490`; invokes the current mode record's action callback. |
| `FUN_005a7670` | The ordinary attack callback. Without an active skill and on the stationary path, calls `FUN_005a6d50`. |
| `FUN_0063e6b0` | Selects the mode's missile field. Mode 8's selector at `0x006ea958` is MonStats offset `+0x3e` (MissS1). |
| `FUN_005a6d50` | Reads that missile and requests it through `FUN_0056ecb0`; its ordinary level is MonsterSkillBonus + 1. |

The event scheduler and dispatcher supply the missing link between the
AI's S1 choice and its missile request. The full AI decision, resource
checks and missile's later behavior are outside this claim.

## Re-check on your own copy

Import your own game.exe into Ghidra as described in tools/ghidra. Read
the functions above with `DecompileAt` and the surrounding instructions
with `Disasm`. Keep the outputs local. On Windows the Ghidra launcher
is `support/analyzeHeadless.bat`; on other platforms use analyzeHeadless.

```sh
analyzeHeadless tools/ghidra/project D2Decomp -process game.exe -noanalysis -readOnly \
  -scriptPath tools/ghidra/scripts \
  -postScript DecompileAt /private/path/council.decomp.c 005e0490 005a75c0 005539b0 005a7ba0 005a7670 0063e6b0 005a6d50
```

Use a built `mpq-cat` and a private output directory to obtain your own
animation record. `-d` writes the binary file directly, avoiding Windows
stdout text-mode conversion. The data and generated Ghidra output must
not be committed or attached to a report.

```sh
mpq-cat -d /private/council-assets /path/to/MPQ-folder 'data\global\animdata.d2'
cd tools/emu
uv run python council_s1.py --exe /private/game.exe --animdata /private/council-assets/data/global/animdata.d2
```

On 2026-11-22, **288 scheduler/dispatch fixtures passed** (classes 345,
346 and 347, each at four starting game ticks and twenty-four effective
animation rates). The checker verifies the action and mode-end deadlines.
The original
`FUN_005539b0` runs with the local animation record. Its event sink is
captured, then its one action is delivered manually to original
`FUN_005a7ba0`. Original mode lookup, action callback and missile-field
lookup reach a captured missile request at level 1. The scheduler
itself produces no immediate missile request.

With explicit `--exe` and `--animdata`, this checker does not require
`D2_MPQ_DIR` or a configured `d2d.cfg`: MPQ-cache initialization is deferred
until `Emu.mpq_file` is first used. The checker was also re-run with MPQ
configuration lookup forced to fail, and its 12 fixtures still passed.
Regression checks for deferred initialization and the first-read path run
with `uv run python -m unittest test_emu_cache` from `tools/emu`; they use
synthetic metadata and require no game files.

### Limits

- Fixtures supply synthetic MonStats/MonStats2 records for these three
  classes, with stationary S1, no active skill entry and distinct
  synthetic missile identifiers. The stock names and movement fields
  were checked separately in the local MPQ text tables.
- The starting-frame helper is replaced with zero. The original
  `FUN_00623b10` returns zero for an ordinary, untransformed monster;
  the fixture does not exercise its unit-conversion helper.
- The event sink is captured rather than running the real event queue.
  Delivery is manual, so queue ordering, interruption and cancellation
  are not tested.
- Difficulty lookup supplies MonsterSkillBonus 0; the missile-creation
  sink records the request and returns zero. No real projectile or
  post-spawn hook is executed.
- No full AI choice, damage or to-hit comparison is claimed.

## The built-d2d differential

`test_action_tick --council` prints the Council's S1 in d2d over walk %
25..175 and anim % -85..75 (105 rows: class, walk %, anim %, d2d's rate
and tick). `council_s1.py compare` runs game.exe's `FUN_00623f50` on each
row (mode_rates.py's rig and hooks: the stats are the row's) and then
`FUN_005539b0` with that rate on the stock HPS1HTH record, and compares
rate and tick:

```sh
set -o pipefail; build/tests/test_action_tick --council | grep '^D ' > d2d.tsv
cd tools/emu && uv run python council_s1.py compare ../../d2d.tsv
```

Result (2026-11-22): **105 rows, 0 differ**. A row with its tick changed
by one is reported (`DIFF ...`, exit 1). Chill changes velocitypercent
and other_animrate alike (`chill_pct`), so it is inside the grid. Not
covered: IAS, FRW (S1 here is a moving mode: FRW would count), the event
queue's own ordering and interruption.

### Built d2d comparison

On 2026-11-22, `ctest --test-dir <build> -R '^test_council_s1$' --output-on-failure`
ran the same three class IDs through d2d's `set_mode` and `monster_update`.
Before the fix, the comparison gave the values below.
All three emitted exactly one missile per case and had the same timings.
The end columns compare the original's scheduled type-1 event with the
first update where d2d left S1.

| Effective rate | Original action | d2d action | Original end | d2d end |
|---:|---:|---:|---:|---:|
| 170 | 5 | 5 | 22 | 21 |
| 200 | 4 | 4 | 18 | 18 |
| 210 | 4 | 4 | 18 | 17 |
| 254 | 4 | 3 | 15 | 15 |

At rate 254 the old action deadline scaled 153 ms to 120 ms. For the
end deadline it scaled the already truncated `14 * 51 = 714` ms
duration. At rate 210 this gave 680 ms, so d2d left S1 at tick 17.

The d2d fixture supplies a synthetic walk base and movement modifier to
exercise the same effective rates. These class IDs take d2d's moving-mode
rate path for S1. The comparison leaves open which effective rates a
stock encounter can reach.

### Restart and slowdown at tick 2

- Restart before the first shot: the original's mode-change function
  `FUN_005a7c20` uses `FUN_00553990` to clear event types 0 and 1.
  Running that helper followed by `FUN_005539b0` at tick 2 replaces the
  original schedule with action at tick 6 and end at tick 20. The d2d
  fixture restarts S1 with `set_mode` and observes one missile at tick 6
  and mode end at tick 20 for each Council class. This does not drive
  the original AI's decision to restart.
- Slowdown before the first shot: the original chill function
  `FUN_0057af80` calls the rate refresh `FUN_00623f50` and queues
  chill expiry, leaving the action at tick 4 and mode end at tick 18
  in the captured schedule. The fixture hooks rate refresh to supply
  rate 100 and captures queue calls. In d2d, injecting that same
  effective rate at tick 2 previously delayed all three missiles to
  tick 8 and left mode end at tick 18. This compares the chill caller's queue
  operations with d2d's deadline calculation, without running the
  original's actual event queue or rate derivation.

The fix calculates both deadlines when ordinary Council S1 starts.
It divides the action frame and animation length, in 1/256-frame
units, by the effective animation rate and rounds up once to whole
ticks. A restart replaces both deadlines; a later rate change leaves
them scheduled.

On 2026-11-22, the built regression passed for all three Council classes
at every rate printed by the checker. A slowdown at tick 2 kept the shot
at tick 4 and the end at tick 18. A restart at tick 2 moved the shot to
tick 6 and the end to tick 20. Each of those cases emitted one missile.

### Freeze at dispatch and stock rates

At the due tick, original `FUN_005a7f80` calls the action callback if
state 1 is clear and drops it if the living monster is already frozen.
The checker scripts the state query and captures the action callback.
The d2d fixture passed for all three classes: freeze before update at
tick 4 suppressed the shot through thaw, and freeze at tick 5 left the
shot that already fired.

`tools/emu/council_event_order.py --exe <local game.exe>` runs the
original timed insertion and dispatch. Within the monster bucket,
the older entry runs first. When a think was filed first, its
mode-change clear (`FUN_00553990`) removes the still-pending action.
If the action was filed first, the think cannot undo it. Player
action events dispatch before monster action events in either
insertion order. The probe substitutes the think decision and
player action callback; it does not run player damage calculation.
In d2d, player modes are processed before the monster update, and
the Council test checks a real get-hit mode change on either side
of that update as well as both insertion orders for the AI think.

With the stock MonStats rows, the original rate helper
`FUN_00623f50` (tools/emu/mode_rates.py) returned these S1 rates for
velocitypercent 75, and for that input plus each difficulty's
coldeffect (-50, -33, -25). No extra states or modifiers were supplied.
The d2d test checks its rate calculation from the stock base walk
animation speed of 168 and MonStats velocities 6, 8 and 10. Both
scheduler fixtures exercise the resulting rates.

| Class | No chill | Normal chill | Nightmare chill | Hell chill |
|---:|---:|---:|---:|---:|
| 345 | 126 | 42 | 70 | 84 |
| 346 | 168 | 56 | 94 | 112 |
| 347 | 210 | 70 | 117 | 140 |

### Council A1 melee timing

The original stationary scheduler queues the Council's `HPA1HTH` melee
action and end at the ticks below. The d2d columns show the built
regression before applying the same fixed-point deadline calculation
as S1 to ordinary Council A1.

| Attack speed | Original action | d2d action | Original end | d2d end |
|---:|---:|---:|---:|---:|
| 50% | 16 | 15 | 31 | 30 |
| 67% | 12 | 12 | 23 | 23 |
| 75% | 11 | 10 | 21 | 20 |
| 100% | 8 | 8 | 16 | 15 |

`test_council_a1` checks the original values against d2d's melee
update for each Council variant. It checks both explicit same-tick
orders: freeze before the update suppresses the swing through thaw,
and freeze after the update preserves the swing. It also checks that leaving A1
before dispatch on the due tick cancels it. These cases establish what
happens when the interruption is applied first; they do not compare
the scheduling order of two independently queued events.

The renderer uses the accumulated pose for locally simulated Council
attacks. The original animation step `FUN_00623e00` adds the current
rate to the existing position. With A1 at 168 for two ticks then 84,
the checker observes positions 168, 336, 420, 504, 588, 672, 756 and 840
in 1/256-frame units. The d2d regression checks that same pose history
while the melee action still resolves at tick 8. This test drives the
animation step and scheduled action separately.

Mode changes clear the scheduled action and pose. Starting a tracked
mode initializes its visible position and tick. The local view copies
the simulated pose, and d2d LAN protocol 11 carries the optional pose
in monster state updates. A joined d2d client draws that position
when present. The original-game TCP/IP path instead receives a monster mode command.
With synthetic point and unit targets, `tools/emu/council_host_packets.py`
runs `FUN_00597e20` and captures A1 commands 11/10 and S1 commands
12/13 in packets 0x6b/0x6c, respectively. Both variants carry the
monster's position correction at +0xc and +0xe. The original client
applies it before dispatching the command (`FUN_0045cf40` / `FUN_0045cfb0`).
d2d applies the correction and uses the aim to face A1, A2 and S1.
For an original host's Council A1 or S1 command, d2d restarts the pose
clock and applies each rate to the existing position. `FUN_00623e00`
gives zero-based A1 frames 1 and 3 at ticks 3 and 6 at rate 168. The
old 60 ms division gave 2 and 4. S1 at stock velocity stays active
until ticks 29, 22 and 18 for the three classes. A raw AnimData duration
ended all three on tick 18.

The client keeps the signed velocity, attack rate, IAS and FRW modifiers
from 0xa8 / 0xaa state lists and removes them on 0xa9. Those fields'
send widths and signs come from the 1.14d ItemStatCost table.
`FUN_0045ee20` and `FUN_0045efa0` feed the state records to the
client, and `FUN_00623f50` gives the animation rate from the unit's
stats. The client keeps the accumulated position across those rate
changes and holds it in freeze state 1, which makes `FUN_004b13a0`
skip the living monster's step. Its clock starts on receipt of the
mode command.

`test_council_a1 --host-pose` dumps every active tick and the end tick
for A1 and S1 on each of the three classes, at stock speed, through a
slowdown and recovery, and through a freeze and thaw.
`tools/emu/council_modes.py` accepts
the dump with `--compare-host-pose` and compares the fixed-point
position and completion to the original executable's `FUN_00623e00`
and `FUN_006217c0` at each tick.
Player, hireling and pet attack clocks use their own paths.

The d2d regressions map original Council IDs 345–347 to reordered
local rows, so both attack paths exercise the loader's ID mapping.

### Other attack timing leads

A base-speed scan of A1/A2 AnimData markers also found `M5A2HTH`:
the Death Mauler record at rate 224 schedules action at tick 10 and end
at tick 18 in the original. The old d2d deadline arithmetic gave 9
and 17. The DeathMaul sequence also uses A2 artwork, with independent
timing. See [DeathMaul in the monster AI notes](monster-ai.md).

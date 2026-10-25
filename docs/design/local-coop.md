# Local co-op — up to 4 pads, one screen

Status: done. Phases 1 to 6 done (PR #57, merged 2026-10-20). Builds on
[net-host.md](net-host.md) stages 1-2 (the N-player World and its rules) and
[controller-plan.md](controller-plan.md) (one pad today).

Goal: friends play together on one screen, from one machine, with no
game.exe running. The target is a docked Steam Deck with external pads; any
PC or Mac with pads works the same way.

## Rules

**Who plays**

- Co-op uses external pads only, at most 4. The first external pad is P1.
- The Deck's built-in controls never take a co-op slot. They still drive
  the Deck itself (Steam, the overlay).
- Handheld, the Deck's controls play as today: single player, or joining a
  game.exe host.
- Off the Deck every pad counts.

**When co-op is on**

- Co-op is on when at least one external pad is connected at game start.
  Docking itself isn't detected; an external pad is the signal.
- The mode is fixed when the game starts. A game begun on the Deck's
  controls stays single player even if a pad is plugged in later. Co-op
  starts from the title with a pad in.

**Joining and leaving**

- P1 picks the first character at character select, as today.
- A pad that comes online mid-game pops up its cursor at once. That player
  picks a character, or makes one, from a roster panel drawn over the game.
  Name entry uses Steam's on-screen keyboard (`main.cpp`, SDL_StartTextInput).
- The new player appears next to the lead, wherever the party is.
- Only characters that match the lead's difficulty (unlocked), hardcore and
  expansion flags can join; D2's own rule. A character already playing is
  greyed out.
- Saves stay local (the usual save directory). Each player's character is
  written on leaving and on quitting.
- A pad that disconnects: its character is saved and the slot is held about
  60 s. The same pad reconnecting takes it back; otherwise the player leaves.
- Save and Exit logs out the player whose pad has the menu; the others keep
  their own characters on their own pads. The lead quitting: the next
  player becomes lead (still on their pad) and the game goes on. The
  difficulty stays (every player matched it on joining).
- A logged-out pad comes back with Start: a seat and its roster.

![Player 2's roster over the game: Alpha greyed (playing), the green cursor on Beta](https://github.com/psi29a/d2decomped/raw/pr-assets/pr57-roster.png)

**The lobby** (2026-10-21, `apps/d2d/lobby.{hpp,cpp}`, toggle `coop_lobby`)

- The title's first button reads "SINGLE PLAYER / COOP".
- On character select, with two or more external pads, the first is
  player 1's. It alone plays the screen, and only it (or the keyboard
  and mouse) starts the game.
- Each other pad has a status line right of the panel. A (or Start)
  opens its roster. Player 1's pick is greyed there, and so is a save
  another partner took. A picks a save, or makes a new character (class,
  name), and the seat reads "ready". B takes the pick back.
- Player 1 picking another character drops the partners' picks, since
  they were made against its difficulty, hardcore and expansion flags.
- When player 1 starts, the ready seats join at once. A seat still
  choosing gets its roster in the game as before.

**Cursors**

- When 2 or more pads are connected, each cursor is tinted by its pad: P1
  red, P2 green, P3 blue, P4 magenta. A colour stays with its pad when
  others leave.
- The cursor is a palette-indexed DC6; the tint is a colour multiply on the
  cursor's blit only.

**One screen**

- Camera: the centre of all players, leashed. Nobody walks past the screen
  edge.
- Warps, waypoints and portals move the whole party. One screen means one
  level.
- Panels (inventory, stash, store, skill tree, roster): one player owns the
  open panel until it closes; the others keep playing.
  A partner takes the screen (`Town::take_focus`) by opening a panel, the
  game menu or the skill picker, or by an NPC or the stash opening to them,
  while the first player has nothing open: the UI shows their character and
  their pad drives the cursor, while the first player's pad plays the field.
  The screen is the first player's again once the partner's is all shut.
- With two players in or more there's no HUD (orbs, belt, skill buttons,
  mini-panel; the pad's skill picker still shows). Every player is in the
  party: their portraits run down the frame's left edge, one under the
  other, in widescreen's spare width when there is some (clear of the
  800x600 UI), each with a life and a mana bar, the class icon and the
  name in the seat's colour; a 16:9 TV's ~133 px leaves room beside them
  for more, the Deck's own 1280x800 80 px (`Town::coop_hud_hidden`, `Town::draw_coop_portraits`). A joined game's
  portraits stay game.exe's row along the top. The Party screen (P)
  lists them as members of the screen's player's party; there's nothing
  to invite, leave or set hostile (`PartyPanel::Own::fixed`).
- A joining pad's roster sits down the left, clear of the middle and of
  the portraits.

![Player 2's inventory, the screen theirs, the green cursor on it](https://github.com/psi29a/d2decomped/raw/pr-assets/pr57-panel.png)

**Death and resurrection** (decided 2026-10-19)

- A player who dies while a partner lives becomes a ghost: drawn
  translucent, floating along with the party; it can't attack, pick up,
  trade or be hit. Its body stays where it fell.
- D2's death penalty applies (experience, gold), but the gear stays on the
  player: no corpse holds it, so there is no corpse run.
- Every co-op player has a **Resurrect** skill. Put on the right button
  and used on a body, it channels for 10 s: a gold aura round the body,
  faint at first, glowing brighter and strobing faster until the dead
  rises there.
- Others can join in: every player channelling on the body adds the same
  speed (two: 5 s, three: about 3.3 s). The progress is the body's.
- Moving, using another skill or a hit that makes a channeller flinch
  (get-hit) takes that player out; the progress is lost only when nobody
  is channelling any more.
- A ghost nobody raised is revived when the party reaches a town.
- Everyone dead: everyone respawns in town, as in single player.
- The skill exists only in co-op. A save never stores it on a button (the
  previous right skill is written instead), so .d2s files stay game.exe's.

![The ghost and the aura through a channel (a mockup over d2d screenshots)](https://github.com/psi29a/d2decomped/raw/pr-assets/pr57-resurrection-mockup.png)

In the game (`World::raise_step`, `draw_raise_aura`): Beta's ghost over the
body, Alpha's channel half done.
ponytail: the channeller stands (no cast animation); the raised get full life.

![Resurrect in the game](https://github.com/psi29a/d2decomped/raw/pr-assets/pr57-aura.png)

## Fidelity

- Co-op on one screen is a d2d addition; it goes in
  [deviations.md](../research/re/deviations.md). Single player stays as it
  is.
- Player-count rules follow game.exe exactly, as traced in net-host.md
  section 2: monster HP and exp scaling (0x6e1590 / 0x6e15b4), NoDrop with
  `near` and `players`, the party exp split (FUN_0057e6c0), the gold split
  (FUN_00540900), party and area quest credit. Co-op players are one party.
- Which game type (+0x6a) co-op counts as decides the death gold rule
  (FUN_005357d0 checks type 3 only): net-host.md open question 1.

## Phases

| # | What | Size |
|---|---|---|
| 1 | The N-player World: net-host.md item 1 (`PlayerSession` / `GameSession`, tagged commands, `quests(player)`, `create_game` / `join`). Each player gets their own View; the others draw through the View's existing `others` list. | 1.5-2 weeks |
| 2 | Player-count rules: net-host.md items 4 and 5 (scaling, NoDrop, party exp, gold split, quest credit, TP per player). | 1-1.5 weeks |
| 3 | Pads become players: up to 4 open, one `pad::Mapper` each, hotplug opens a slot, the 60 s hold. Tell the Deck's built-in controller from external pads (below). | 2-3 days |
| 4 | The roster panel and tinted cursors: pick or make a character in-game, the join rules above. | 3-4 days |
| 5 | One screen: the centroid camera with its leash, party warps, panel ownership, per-player saves, the lead hand-over. | 3-4 days |
| 6 | Death and resurrection: ghosts, the Resurrect skill and its channel, the gold aura, revival in town. | 3-4 days |

Not needed for co-op: net-host.md item 2 (room activation for players on
different levels). One screen keeps the party on one level.

## Checks

- Phase 1 keeps every existing test green: test_protocol, the per-act
  checks, smoke_d2d, and the netplay join path (`apps/d2d/netgame.cpp`).
- Two to four virtual players driven by devctl, as net-host.md stage 1.
- tests/coop_d2d.py: devctl's virtual pads (one with the Deck's ids) take
  seats, join, walk their own players, replug.
- The built-in vs external split needs a real Deck. Steam Input shows every
  pad as a virtual Xbox pad; SDL 3 should still report the real device
  behind it (the Deck's controller is USB 0x28de / 0x1205). Each pad's
  real type and ids are logged as it connects ("gamepad: ... connected"); one run on a docked Deck with a pad settles
  it. SDL reads the real ids from the file Steam names in
  `SteamVirtualGamepadInfo`; whether the Flatpak sees that file is logged
  too ("gamepad: Steam's pad info ..."). The first Deck run had the Deck's
  own controls playing player 1 docked.

## What it does for networking

- Phases 1 and 2 are net-host.md stages 1 and 2. A d2d host needs them
  as they are.
- Phases 3 to 5 are local only (input, one screen). The roster and
  per-player saves carry over to a host's join.
- Still left for hosting after co-op: the D2GS codec halves, the
  `PacketEmitter`, the join handshake, room activation (net-host.md items
  2, 6-10).

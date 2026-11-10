# Controller assignment and touch prototype

UX decisions for issue #144, researched 2026-11-09. These are client-side
controls, not claims that stock game.exe supports touch or these bindings.

## References and what they support

| Game | Source and observed design | Decision here |
|---|---|---|
| Diablo II: Resurrected | Blizzard's [console design article](https://news.blizzard.com/en-us/article/23722094/bringing-the-original-diablo-ii-experience-to-console) describes direct skill buttons, an LT bank of six additional skills, a directional targeting cone, and corpse selection appropriate to the ability. It also discusses the difficulty of choosing one drop among many. | Keep direct buttons and LT. Resolve the displayed Attack/Use action once for its press. A held attack does not become pickup when an enemy dies. Corpse abilities seek eligible corpses, not live enemies. |
| Diablo Immortal | The [original Blizzard announcement](https://investor.activision.com/node/32001/pdf) describes dragging a skill to aim and releasing to use it. Blizzard's [accessibility article](https://news.blizzard.com/en-us/article/23805083/making-a-game-for-everyonediablo-immortals-accessibility-features) documents controller remapping, a free UI cursor, and repositionable mobile skill buttons. | Independent movement and skill fingers; tap or drag/release with an explicit cancel region. Keep assignment identities independent of controller positions. The prototype has fixed positions; configurable positions remain outside this change. |
| Grim Dawn | First-hand reports on the developer-hosted forum describe enabling extra buttons in [Options > Controller](https://forums.crateentertainment.com/t/on-a-controller-how-do-i-assign-more-skills/114059). The [first-hand skill-panel assignment reports](https://www.reddit.com/r/Grimdawn/comments/1m2mysp/) about its radial are community evidence, not a developer specification of exact current glyphs. | Put a visible Assign action in the skill panel. Show actual configured destinations and their occupants. Do not assume every player uses the default controller layout. |
| Path of Exile 2 | GGG's [0.2.0 patch notes](https://www.pathofexile.com/forum/view-thread/3740562) document per-skill controller targeting, corpse-targeting fixes, panel improvements, and pickup interaction fixes. | Auras/self-casts, enemy attacks, corpse abilities and ground placement have different targeting policies. A generic nearest-enemy policy must not cause buffs to walk. |

The right-hand column is our design inference. Other games are UX references;
Diablo II 1.14d remains the evidence for original gameplay.

## Controller

- In Skill Buttons mode, highlight a learned, bindable skill in the skill
  panel. Y (the north-position glyph for your controller) opens **Assign to
  button** with that skill selected. The panel returns when Done is pressed.
- View remains the field shortcut. It opens **Skills**; D-pad left/right
  switches **Skills / Actions**, and up/down changes pages. Inventory is in Actions
  and remains available through Start's panel menu.
- The editor shows bank I/II, actual configured destination glyphs, and their
  occupants. Hold LT to preview/assign bank II. A destination assigns and
  shows its resulting binding; the editor stays open for another assignment.
  View or Start is Done. Destinations are consumed until all buttons release.
- First activation seeds RT from the current left-click ability, RB from the
  right-click ability, then available A/B/X/Y/LB and alternate destinations from
  eligible saved hotkeys in order. Existing sheets are read without adding
  defaults: a saved Clear remains Clear.
  Legacy files with one RT skill still use it for LT+RT until explicitly set.
- Attack/Use shows its binding, verb and target. Only a fresh press can pick
  up, talk or use. Combat may continue while held. Dedicated Interact remains
  available. Any LT skill chord suppresses tap-to-swap, in either release order.
- B (Back's configured button) closes panels. RS (Interact's configured button)
  uses inventory items. Identify prompts for a valid unidentified item; empty
  books and town-only restrictions are reflected in prompts and dispatch.
  Stock shows Buy, sellable bag items show Hold to sell. Transaction rules stay
  in the existing commands; the vague reported shop issue is not treated as a
  diagnosed economic bug.

## Touch prototype

Enable `--touch-controls` or `touch_controls = 1` in d2d.cfg. This is an SDL
input prototype, not an Android/iOS packaging or device-support claim. Coordinates
follow the game's existing logical viewport. Use landscape for this prototype.

- Left movement area, fixed large Attack/Use, four skill buttons, and a
  one-tap I/II page switch. Belt buttons are available on either page.
- Skills opens the original skill panel. Tap a learned skill, then **Assign
  to touch button**, then a destination. Occupants are visible before replacing
  them. Done returns to the panel; Back closes it.
- Per-character `<name>.touchskills` files are separate from `.padskills`;
  touch positions 1–4 on each page do not borrow controller destinations.
  Both sidecars preserve ordinary skill IDs and never modify `.d2s` hotkeys.
- Tap to use the shown target. Drag from an aimed skill to select a direction
  and range, then lift to cast. Teleport previews the existing engine's room,
  line and nearest-free-subtile constraints. Corpse skills preview usable
  corpses and can change their selection by dragging. Auras and self-casts
  stay on the caster. The active aura is labelled on either page.
- Drag to the visible cancel region or interrupt the skill finger to cancel.
  Panels and focus loss invalidate pending gestures. Only a valid lift commits
  a cast, including stationary taps, so cancellation spends no mana.
- Movement and skill fingers have separate ownership. Lifting either does not
  commit or release the other. Changing page does not release movement or
  retarget an already-held gesture. Attack pickup needs a fresh press.
- Tap a usable carried tome/potion to use it. Identify then accepts a tap on an
  unidentified item. Tap merchant stock to buy; hold a sellable carried item
  to sell. These feed the existing item and trade commands.

Charged skills granted by items remain a separate design question: an item ID,
remaining charges and save lifetime must be designed before allowing them into
these assignment files. No new art, dependency, network service or launcher is
needed. The new `touch` devctl verb injects ordinary SDL finger events for tests.

## Original-game evidence reused

These citations are existing research/ports, not newly oracle-verified claims.

| Address | Existing evidence |
|---|---|
| `004625b0` | Click dispatch; `docs/research/re/cursor-units.md`. |
| `00462930` | Held-unit gate preserves pending interactions; `docs/research/re/collision.md`. |
| `00487740` | Item use; existing inventory right-click handler. |
| `004917c0` | Inventory right click; existing inventory handler. |
| `005c9480` | Self-state skills; `docs/research/re/skills.md`, `game::self_cast`. |
| `005ca360` | Teleport; existing do-27 port in `fight/strike.cpp`. |
| `005cf010` | Friendly aura pulses including Might; `docs/research/re/skills.md`. |
| `0063a770` | Usable corpse selection; `Monster::usable_corpse`, `docs/research/re/skills.md`. |
| `0064e7b0` | Free subtile for Teleport; existing do-27 port. |

## Maintainer playtest

1. Choose **Skill Buttons** in Configure Controls. Open the skill panel,
   highlight Might, choose Assign, and bind it to RB. Remap RB/LB, repeat, and
   check the destination labels. Assign a second-bank skill with LT. Press
   Done, save, quit and reload. Test an old one-RT file and a deliberately empty A.
2. Hold Might near an enemy: it must toggle once without chasing. Change bank:
   its active aura label must stay visible. Tap LT to swap weapons; try both
   release orders of a quick LT+skill chord and confirm neither swaps weapons.
3. Open Character, Inventory, Skills, Quests and Help; B must close/cancel them.
   Assign Inventory through Actions and verify Start still reaches Inventory.
4. Approach a shrine and a drop: check the displayed verb/target, then press A.
   Kill an enemy while holding A over its drop: keep holding, then release and
   press again. Only the new press should pick up the item.
5. Over both empty and charged identify/portal tomes, check availability text.
   Use Identify, select an invalid item, then a valid unidentified item. Use a
   portal outside town. At a merchant, buy one item and sell it separately.
6. Launch with `--touch-controls --view-width 1066`. Assign Teleport and a corpse
   ability on appropriate characters. Keep a movement finger down while aiming,
   cancelling and switching I/II with other fingers. Cancel by dragging, finger
   interruption and opening a panel: no cast or mana loss. Lift normally and
   check Teleport's shown landing point or the highlighted corpse.
7. Save/reload touch bindings and confirm controller bindings did not change.
   Check the active aura on both touch pages, belt access and the fixed Attack
   button. Repeat tome use and one purchase/sale with touch.

## Validation record

Local validation: the focused controller/touch checks pass using virtual SDL
pads and injected finger events. A temporary-character probe wrote and reloaded
both actual sidecars, including a legacy one-RT controller file and a deliberately
cleared A. The controller input path activated a real generated shrine after
its room was explored, held Attack across a kill without looting, then picked up
on a fresh press. Touch tests used both tomes and bought and sold separately.
A 1066x600 landscape screenshot was inspected. See the handoff for full presubmit
results and any remaining baseline failures. Native controller hardware and a
physical phone are not present in the automated checks. SDL virtual-controller
and injected-finger tests exercise the game input path; screenshots inspect the
prototype layout. Hardware comfort, OS interruption behavior, safe areas and
on-device scaling remain unverified until tested on devices. The full-suite
baseline already has failures in `test_game` and command-driven waypoint travel
in `smoke_d2d`; report these separately from focused controller/touch results.

## Playtest follow-up: selection, Identify and pointer input

D-pad roster navigation owns A confirmation until actual mouse/analog movement.
This intentionally hides and ignores the stale pointer. Identify items are
excluded from modern skill assignment, including old modern bindings becoming
unavailable without rewriting their files. Inventory Use selects Identify; A or Use confirms the target, and B cancels.
Outside Identify mode, A retains normal inventory pickup/movement even for a
usable Identify tome or scroll. Portal skill destinations use a carried charged tome directly in the field.
Missing/empty tomes and town restrictions disable them. These are UX changes, not
newly traced original-game claims.

The opt-in overlay accepts one mouse/Pencil-style pointer as well as independent
SDL fingers. Pointer input cannot emulate two independent thumbs. Sidecar
compatibility requires checking events on the user's actual macOS/iPadOS pair;
no physical Sidecar verification has been performed. No bridge or dependency
is introduced. Rounded skill/attack outlines reuse the existing renderer.

## Radial and movement follow-up

Generic Kick, Identify, Scroll of Identify and Book of Identify are excluded
from modern assignment. Class kick skills remain. Saved excluded entries
are unavailable without overwriting their saved IDs. A portal binding selects
a carried charged tbk and dispatches UseItem once per press. Inventory need
not be open; this supersedes the earlier panel restriction after playtest.

Simulated clean stick release reproduced a facing change from 13 to 8. The
release sends a fresh Move to the player's exact current position. Previously
that requested a new path, whose subtile-centre step could turn the character.
Exact-position fresh Move now clears movement without replanning. This is a
controller UX correction, not newly traced original behavior (not traced).
The configured deadzone and intentional right-stick aiming remain unchanged.

Belt directions are drawn arrows, independent of font arrow support, while
remapped non-direction buttons retain their actual names. Native touch input
cannot currently be tested; Sidecar does not count as independent-finger
validation. Simulated tests remain useful but do not establish device support.

Controller Skill Buttons navigation: the run/walk control shows its actual
remapped stick-click binding and current Run/Walk state. With Party, Quests,
Character, Inventory, Skills or Options open, the configured previous/next
shoulder inputs cycle that order with wraparound. A tab strip shows the current
panel. Held items, Identify, Imbue and gold confirmation block switching with
a visible explanation. Store tabs and classic controller shortcuts retain
their existing behavior. Offline Party is informational.

The radial uses D-pad Up/Down for pages, Left/Right for Skills/Actions and the
aiming stick for selection. A separate 30% centre deadzone (or the user's
larger configured deadzone) preserves selection at rest; a small angular
margin stabilizes wedge boundaries. Destinations and Done remain unchanged.
Skill-tree Up/Down prefers a node in the same column across gaps before
moving sideways. Tree opening focuses a skill rather than a tab button.
These are controller UX decisions, not newly traced original-game claims.

## One-time controller setup and restored controls

A character without a controller sheet gets one when Skill Buttons first
becomes active. RT uses the current eligible left-click ability, RB the
right-click ability. Eligible saved hotkeys fill available A/B/X/Y/LB and then
alternate destinations in saved order, without duplicates. An unbound
destination is left empty. No popularity or build assumptions are used.
Charged item entries are excluded from automatic setup.

The sidecar writes `initialized 1`; an existing sidecar, even an empty or
legacy one without that line, also counts as initialized. Preset changes,
reloads and deliberate clears do not reseed it. Touch assignments stay
independent. These defaults supersede the earlier fresh-A default.

The open mini-panel shortcut strip is no longer hidden by the controller
tray. The skill tree gains a visible, clickable close control using existing
close-button art, with matching drawing and hit coordinates; B remains
available. These are UI fixes, not newly traced original-game claims.

The untraced permanent Character/Stats shortcut has been removed; the original
HUD art and conditional New Stats notification are retained. Character remains
available through its normal key and mini-panel action. The tree close control
now uses retail class/tab placement, rather than the earlier fixed corner.

## Review follow-up: Identify inventory movement

A retains ordinary inventory pickup/movement in both controller presets, including
empty tomes and usable Identify items. Use (RS in Skill Buttons mode) starts
Identify; A or Use then confirms a target. This avoids stealing the only pickup
input when a tome can identify another carried item. The existing inventory
right-click dispatch is reused; this is an input UX correction, not a new retail
parity claim. `pad_identify_classic_d2d` and `pad_identify_d2d` drive both presets
through actual controller input with and without unidentified targets.

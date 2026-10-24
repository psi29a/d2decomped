# Stubs and dead code in game.exe 1.14d

Code that's in the binary but does nothing, or that nothing reaches. Look
here first before porting something that "should" work. Each row links to
the doc with the details. Add a row whenever a trace ends in one.

**Kinds:**
- **stub**: a table entry or call target whose body is a bare return.
- **orphan**: a real function with no caller, raw pointer or rel32 call.
- **unreachable**: real code whose trigger never happens in the shipped data.

| Address | What | Kind | Effect in 1.14d | d2d | Details |
|---|---|---|---|---|---|
| `0x5fd200` | UberBaal think (MonAI 145) | stub (`ret 4`) | uber Baal stands after his target search | `mon_think` returns none | monster-ai.md "AncientStatue and the ubers' MonAIs", bugs.md #26 |
| `0x5f81c0` | UberMephisto think (MonAI 146) | stub (`ret 4`) | uber Mephisto stands | same | same |
| `0x5e9df0` | UberDiablo think (MonAI 147) | stub (`ret 4`) | uber Diablo stands | same | same |
| `FUN_00594270` | Cube portal kind 2, Pandemonium Portal (table 0x6e11c8) | stub (`return 0`) | the three keys stay in the Cube; no way to 133..135 | `World::transmute` refuses | cube.md |
| `FUN_00594280` | Cube portal kind 3, Pandemonium Finale Portal | stub (`return 0`) | the organs stay; no way to Uber Tristram | same | cube.md |
| `FUN_005e9dd0` | makes 0x2c7 (demonhole) at a unit, mode 1, flags 0x40 | orphan | — | not ported | monster-ai.md, as above |
| `FUN_005f7f10` | a make next to Mephisto's think (1.13 uber Mephisto's minions) | orphan | — | not ported | same |
| `FUN_005dd140` | counts uber Mephisto / Diablo / Baal, 0x2c7, 0x2c8, 731..733 in a radius | orphan | — | not ported | same |
| `FUN_005eeaa0` | AncientStatue think (MonAI 132): Rite awake → skill 302 | unreachable | ancientstatue1..3 are never placed or made | not ported | monster-ai.md, as above |
| `FUN_00594700`, `FUN_005947e0`, `FUN_00595160` | a1q4 list append / contains, and another | orphan | — | not ported | quests-act1.md |
| `FUN_00570530` | a monster AI restore (calls the AI toggle) | orphan | — | not ported | monster-ai.md "Curse thinks and other thinks" |
| `FUN_005e2d80` +0x1c | a riding imp's two extra draws when its AI control +0x1c is set | unreachable | the init (FUN_005e2cd0) copies 0x6e34e0's flags, both 0 | not ported | monster-ai.md "The imps' ride and skills" |
| `FUN_005a30e0` mod 24 | thief (MonUMod 24) on a hit | unreachable | MonUMod disables it; `FUN_005a49b0` skips it for Lord De Seis | not ported | monsters.md |

Still reachable, though the content looks dead: the uber levels 133..136
build and populate (`FUN_005559a0`'s uber cases, the Hellfire Torch hook
`FUN_005e0070`, UberIzual's real think `FUN_005f8c80`). d2d ports them
all, so they work once something takes a player there.

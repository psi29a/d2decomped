# Legal notes

D2Decomp is a hobby project. It is not for profit: nothing is sold, there are no
ads, and no one is paid for it. The goal is simple:
to play the copy of Diablo II: Lord of Destruction we bought, on the computer
and operating system we choose, together.

This page explains why the project is built the way it is, and which parts of
EU law it relies on. It is not legal advice.

## What the repository contains

- **Our own code only.** Every line of the engine is written for this project
  and licensed under the GPL-3.0-or-later.
- **No Blizzard files.** The repository holds no game data (MPQs, sprites,
  sounds, videos, tables), no game executables or libraries, no copied code,
  and no dumps of the original program's bytes. You supply the game from your
  own installation or media, and D2Decomp reads it at run time.
- **Notes and tests.** Research notes describe how the original behaves: file
  formats, rules, network messages. Test tools run functions of *your own*
  copy of the 1.14d `Game.exe` (with Unicorn) to check that our code behaves
  the same. Those tools load the program from your disk; they do not ship it.
- **One piece of licensed artwork.** The app icon's goat skull and pentagram
  (`apps/icon/`) are *Designed by dgim-studio / Freepik*, used under the
  Freepik license, not the GPL. The binary "decompiling" half is ours.

## The EU Software Directive

Directive 2009/24/EC on the legal protection of computer programs sets the
rules all EU member states follow. Three of its provisions matter here.

1. **Article 1(2): ideas are not protected.** Copyright protects a program's
   *expression* (its code), not the ideas and principles behind it, including
   those behind its interfaces. The Court of Justice confirmed in *SAS
   Institute v World Programming* (C-406/10, 2012) that a program's
   functionality, its programming language, and the format of its data files
   are not protected expression. Anyone may write new code that does the same
   thing, as long as they don't copy the original code.
2. **Article 5(3): observing, studying and testing.** A person with the right
   to use a program may observe, study and test how it works, to find the
   ideas and principles behind it, while loading, displaying or running it.
   This is what running the game and our test tools against `Game.exe` does.
3. **Article 6: decompilation for interoperability.** A lawful user may
   reproduce and translate a program's code (decompile it) when that is
   necessary to get the information needed to make an independently created
   program interoperate with other programs, under these conditions:
   - the person is a licensee or otherwise has the right to use the program;
   - the information was not already readily available to them;
   - the work is limited to the parts of the program needed for
     interoperability.

   The information obtained may not be used for other goals, given to others
   except when needed for interoperability, or used to make a program that is
   substantially similar in its *expression*.

**Article 8: a license can't take this away.** Contract terms that forbid what
Articles 5(3) and 6 allow are null and void. An end-user license agreement
cannot remove these rights from an EU user.

## How D2Decomp stays inside those lines

- **Interoperability is the purpose.** D2Decomp has to read the game data
  files you own, write save files the original can load, and (planned) speak
  the original's network protocol so we can join games hosted by the original.
  Blizzard publishes no specification for any of these, so the information
  isn't available any other way.
- **Independent creation.** The engine is new code in modern C++, with its own
  structure and design. We study the original to learn *what* it does, the
  formulas, file layouts and message formats, then write *how* ourselves. No
  decompiled code is copied into the engine.
- **Limited use of the findings.** What we learn goes into this engine and its
  documentation, for the purpose of running and interoperating with the game.
- **Lawful copies.** Every contributor and every player uses their own
  purchased copy of the game. D2Decomp is useless without one.

## Honest limits

- **Scope is debated.** Article 6 was written mainly with program-to-program
  interfaces in mind. Whether reproducing the behavior of a whole program to
  run its data falls entirely within it hasn't been settled by the Court of
  Justice. Projects like ours rely on the combination of Articles 1(2), 5(3)
  and 6, and on never copying the original's code or assets.
- **Other countries differ.** Outside the EU other rules apply; in the United
  States, for example, 17 U.S.C. § 1201(f) and fair use cases such as *Sega v.
  Accolade* (1992) cover similar ground, but license terms can carry more
  weight there.
- **Blizzard's online services are a separate question.** Their terms forbid
  unauthorized connections to Battle.net. D2Decomp does not connect to
  Blizzard's servers (see [docs/design/battlenet.md](docs/design/battlenet.md)).

## Trademarks

Diablo, Diablo II, Lord of Destruction, Battle.net and Blizzard Entertainment
are trademarks of Blizzard Entertainment, Inc. They are used here only to say
which game D2Decomp works with. D2Decomp is not affiliated with or endorsed by
Blizzard Entertainment.

## Contact

If you are a rights holder and believe something in this repository crosses
these lines, please open an issue and we will look at it promptly.

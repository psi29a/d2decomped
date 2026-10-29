# Refactor plan: separate the game, engine, and content

## Mission

Review and incrementally refactor D2Decomp so that:

1. Diablo II remains a faithful, playable first game.
2. Game-independent engine code can be tested and reused without Blizzard game data.
3. D2 content and rules have clear boundaries, making them easier to inspect and extend.
4. A future editor could browse, validate, preview, and author content.
5. A future game could use the engine without inheriting Diablo II's rules or asset formats.

Do not begin by moving large directories or building an editor UI. First inspect the current architecture, document the real dependencies, and propose small changes that prove the boundaries.

## Current baseline

The repository reports that all five acts and the 27 quests are playable through Baal's death. Treat Acts I–V as the existing reference game and preserve that behavior.

The project still has D2-specific work to track separately, including Uber-level monster/object placement accuracy, live multiplayer validation for Act V, and incomplete sound coverage. D2-hosted multiplayer/server work is also a separate project concern.

The current `components/game` and `components/rules` code should not be assumed to be generic merely because they are separate libraries. They contain D2 concepts, asset formats, IDs, and behavior. Existing tests that load D2 game data do not by themselves prove the engine can run without it.

## Target conceptual boundaries

Use these as architectural goals, not as a requirement to rename or reorganize everything immediately.

### 1. Engine/runtime foundation

Keep only capabilities that can be described without Diablo II concepts, such as:

- Core runtime lifecycle and update scheduling.
- Platform and client-facing abstractions where they are truly shared.
- Common math, identifiers, serialization primitives, and deterministic random-number facilities, if their current contracts are game-independent.
- Narrow interfaces for content access and game-specific behavior.

Do not extract a “generic engine” by renaming D2 types or wrapping D2 assumptions in generic-looking interfaces.

### 2. Diablo II game module

Keep game-specific behavior explicit and owned by the D2 module, including:

- Combat, skills, items, quests, progression, monster AI, and D2 save/protocol behavior.
- Mapping from original D2 IDs and data formats to runtime definitions.
- Behavior that requires D2-specific code, even if some of its parameters come from data files.

Do not force quests, AI, or map generation into a fully declarative format before the code demonstrates that this is practical.

### 3. Game project and content

Treat Diablo II's content as one project consumed by the runtime. It includes:

- Acts, levels, transitions, quests, monster and object placements.
- Archetypes, item and skill definitions, localization, audio, and visual assets.
- Imported data from original formats such as MPQ archives and their TXT/BIN/DS1/DT1 data.

Keep the original-format loaders as adapters. Avoid spreading MPQ path rules, TXT column names, or retail numeric IDs throughout otherwise reusable runtime code.

### 4. Editor-facing content services

Build APIs that a future editor could use before building the editor UI:

- Enumerate projects, acts, levels, archetypes, assets, and references.
- Inspect content through stable symbolic identifiers.
- Validate missing references and malformed definitions.
- Preview a level or generated layout with a known seed.
- Import and export authored definitions without requiring a running game client.

Editor support must account for both data-driven content and code-driven algorithms. Not every act-specific behavior should be represented as editable rows.

## Work plan

### Phase 0: Establish an accurate map

Inspect the latest repository state before proposing code changes.

- Read the current README, project plan, build configuration, tests, and component dependency rules.
- Trace how a D2 asset enters the system, becomes a runtime definition, and is used by gameplay.
- Identify which libraries can compile and run without SDL, client code, or Blizzard assets.
- Identify D2 assumptions in supposedly reusable libraries: retail IDs, fixed act/level ranges, MPQ/TXT dependencies, D2 save structures, and D2-specific enums or names.
- Check whether the plan's status and historical Act IV/V notes agree with the latest README and code. Update documentation only if asked to implement the plan; otherwise list the corrections needed.
- Produce a dependency diagram and a short inventory of current boundaries before recommending directory moves.

Deliverable: an evidence-based architecture map and a prioritized list of boundary violations.

### Phase 1: Define small contracts

Propose the minimum interfaces needed to separate the D2 project from the runtime.

- Use stable symbolic IDs in new boundaries; map legacy D2 numeric IDs at the import edge.
- Define a content access contract that can serve MPQ-backed D2 content and a future native project directory.
- Define a distinction between authored definitions and executable game behavior.
- Keep each act's distinct layout-generation algorithm behind an explicit strategy or module boundary. Do not flatten all acts into one assumed universal generator.
- Keep game-specific extension points narrow. Do not introduce a general plugin or scripting framework without a concrete use case.

Deliverable: interface sketches and examples showing how existing D2 data crosses each boundary.

### Phase 2: Prove a D2-free core

Choose one small capability that has a clear game-independent contract and extract it incrementally.

- Add or adapt a minimal test fixture that uses no Blizzard files, MPQ loader, D2 IDs, or D2 save data.
- Make the fixture exercise the proposed runtime/content boundary rather than just compile a library.
- Keep current Diablo II behavior working through the existing content path.
- Avoid moving the entire `components/game` library in one change.

Acceptance: the small fixture can run using only the proposed engine/runtime APIs, while the D2 client still consumes D2 content and rules.

### Phase 3: Migrate D2 through adapters

Move one vertical slice at a time, starting with the clearest and least coupled content path.

For each slice:

1. Identify the source data and existing runtime consumers.
2. Parse legacy formats in a D2-specific adapter.
3. Convert parsed data into stable runtime definitions.
4. Update consumers to use the new contract.
5. Check that the D2 experience retains its current behavior.
6. Record any data that remains code-driven and why.

Possible slices include level metadata and transitions, archetype definitions, or asset lookup. Select based on actual coupling found during Phase 0 rather than assuming an order.

Acceptance: reusable runtime code no longer needs to know the legacy file layout for the migrated slice.

### Phase 4: Prepare editor-facing tools

Build headless content tools before an editor interface.

- Add content inspection and validation commands or APIs.
- Report unresolved references with symbolic IDs and source locations.
- Support deterministic level previews where the generator permits it.
- Define how authored content is saved, versioned, and imported.
- Keep proprietary source assets outside the repository and preserve the existing user-supplied game-data model.

Acceptance: a developer can inspect and validate project content without launching the full game client.

### Phase 5: Demonstrate a second project shape

Create a tiny synthetic project or fixture that uses the extracted boundary and has no Diablo II assets or rules.

It only needs enough content to prove the project model works—for example, a small level, a few entities, and one simple game-specific behavior. Do not build a second full game.

Acceptance: the fixture uses the shared runtime and its own content/rules without linking D2-specific loaders or definitions.

### Phase 6: Track Diablo II completion separately

Keep fidelity and product work visible, but do not make engine extraction depend on finishing unrelated features. Track at least:

- Uber-level monster and object placement accuracy.
- Act V live multiplayer validation.
- Remaining sound coverage.
- D2-hosted multiplayer/server work, if still intended.

Preserve the all-acts playable baseline throughout refactoring.

## Guardrails

- Do not modify original game assets or redistribute Blizzard data.
- Do not assume that every table column should directly control behavior.
- Do not assume a game editor is just a CSV editor; layout algorithms, quests, AI, and event behavior may require code.
- Do not replace meaningful D2 names with generic names while retaining D2-specific assumptions.
- Do not choose a custom content format, scripting language, or plugin system before the requirements and migration costs are documented.
- Keep changes reviewable and reversible. Each proposed refactor should name its affected components, behavior risk, and verification method.
- Preserve deterministic generation and reproducible debugging where currently supported.

## Required output from the planning agent

Return:

1. A diagram of the current dependency and content-loading flow.
2. A proposed target boundary diagram.
3. A table of current components classified as engine/runtime, D2 rules, D2 content adapters, client, or tooling—with ambiguous cases called out.
4. A staged migration proposal with small reviewable steps, dependencies, risks, and acceptance criteria.
5. The first concrete refactor slice to implement and why it is the best starting point.
6. A separate list of D2 fidelity work that should not be confused with engine/editor refactoring.

Do not start a broad rewrite. Make the first proposed change small enough to validate the boundary with the existing D2 game and a D2-free fixture.

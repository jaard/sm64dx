# Coopdx Solo Mod Plan

## Goal

Create a mostly vanilla solo Super Mario 64 experience on top of
`sm64coopdx`, keeping coopdx's more stable rendering pipeline while removing
or hiding multiplayer/gameplay-facing systems.

The target product should feel closer to `sm64ex`:

- vanilla intro flow, including the Mario head/title experience where feasible
- standard file select and save-slot management
- standard pause/options UX, not a multiplayer-first frontend
- fixed `30 / 60 / 120 FPS` selection in the video options menu
- macOS app packaging and first-run ROM import behavior similar to this repo
- coopdx rendering stability retained as the rendering baseline

The important architectural inversion is:

- current approach: start from `sm64ex`, port coopdx rendering fixes inward
- proposed approach: start from `sm64coopdx`, strip or gate multiplayer/gameplay
  behavior outward, keeping the coopdx rendering pipeline intact

## Non-Goals

- Do not preserve online multiplayer.
- Do not preserve coopdx's public-server browser, lobby flow, or network UI.
- Do not preserve Lua/mod gameplay features unless they are required by the
  retained renderer or asset pipeline.
- Do not attempt to make save files compatible with coopdx multiplayer saves
  unless that becomes an explicit requirement.
- Do not port rendering systems piecemeal out of coopdx unless a direct coopdx
  base proves impractical.

## Core Strategy

Use `deps/sm64coopdx` as the rendering and engine base, then restore vanilla
solo UX and behavior from `sm64ex` in layers.

The stable rendering systems should remain mostly owned by coopdx:

- matrix interpolation tables
- camera-space matrix correction
- shadow interpolation buffering
- per-delta shadow regeneration
- skybox/background interpolation
- viewport/projection interpolation
- particle and environmental interpolation hooks
- interpolation skip handling

The solo experience should be owned by a small compatibility layer:

- boot/title/menu flow
- vanilla file select
- vanilla save-slot semantics
- simplified options menu
- fixed FPS config choices
- network-disabled startup path
- macOS packaging and importer integration

## High-Level Phases

1. Establish a buildable coopdx baseline in this repo.
2. Add a `COOPDX_SOLO` or equivalent build/config mode.
3. Disable network-first startup without deleting systems immediately.
4. Restore vanilla title/menu/file-select flow.
5. Restore vanilla save management or define a clean solo save format.
6. Add sm64ex-style fixed target FPS options.
7. Package the result with this repo's macOS launcher/importer model.
8. Remove dead multiplayer surface area after the solo path is proven.
9. Validate rendering stability against the known high-FPS failure cases.

## Phase 0: Repository And Build Setup

### 0.1 Add a Coopdx Source Lock

Extend `lock.json` so the repo can pin both upstreams:

- `sm64ex.repo` and `sm64ex.ref` remain for comparison and patch reference
- add `sm64coopdx.repo`
- add `sm64coopdx.ref`

The first solo branch should build from a fixed coopdx commit, not a moving
branch. Rendering regressions are hard enough without source drift.

### 0.2 Decide The Workspace Shape

Recommended directory layout:

```text
deps/sm64ex/          pinned vanilla/reference source
deps/sm64coopdx/      pinned coopdx source
build/coopdx-solo/    disposable patched coopdx build tree
patches/coopdx-solo/  solo patches against pinned coopdx
out/sm64ex.app        packaged app, name can change later
```

Keep `build/sm64ex` intact for the current path until the coopdx-solo path is
proven.

### 0.3 Add A Parallel Build Path

Avoid rewriting the current `build.sh` in place at first. Add either:

- `build-coopdx-solo.sh`, or
- a `./build.sh --coopdx-solo` mode

The parallel script should:

1. ensure `deps/sm64coopdx` exists and is at the pinned ref
2. create `build/coopdx-solo`
3. apply `patches/coopdx-solo/*.patch`
4. build a ROM-free/external-data executable if feasible
5. package with the macOS launcher/importer

The existing sm64ex build path should remain a known-good fallback during the
transition.

## Phase 1: Baseline Coopdx Audit

Before cutting code, inventory coopdx systems that affect the solo path.

### 1.1 Boot And Frontend

Audit:

- `src/pc/pc_main.c`
- `src/pc/djui/`
- `src/pc/loading.c`
- `src/engine/level_script.c`
- `src/menu/`
- title screen and intro level scripts

Questions to answer:

- Where does coopdx enter DJUI instead of vanilla menus?
- Is vanilla file select still compiled?
- Is the Mario head/title path present but bypassed?
- Which systems assume network initialization before gameplay?

### 1.2 Save System

Audit:

- `src/game/save_file.c`
- `src/game/save_file.h`
- `src/pc/platform.c`
- config and preference path handling
- any coopdx-specific save extensions
- any Lua/mod profile storage

Questions to answer:

- Is the vanilla save slot format still structurally present?
- Does coopdx add fields to the save file?
- Does gameplay read multiplayer state from save/load code?
- Can vanilla slot data be restored without touching renderer code?

### 1.3 Rendering Systems To Preserve

Audit and mark as "do not rewrite unless necessary":

- `src/game/rendering_graph_node.c`
- `src/game/rendering_graph_node.h`
- `src/game/shadow.c`
- `src/game/skybox.c`
- surface interpolation hooks
- particle interpolation hooks
- camera interpolation skip logic
- viewport/projection patching
- `gRenderingInterpolated` behavior

This is the part coopdx should keep owning.

### 1.4 Gameplay Mod Surface Area

Identify coopdx changes that are user-visible in solo play:

- multiplayer player model/state
- nametags or player indicators
- network object sync
- chat or console overlays
- Lua behavior hooks
- coop-specific camera or pause behavior
- extra debug/dev menus
- non-vanilla object behavior changes

Classify each as:

- must remove
- can compile out
- can hide in solo mode
- can keep because it is invisible in solo mode

## Phase 2: Introduce Solo Mode

### 2.1 Add A Central Build Flag

Use one central mode flag, for example:

```c
#define COOPDX_SOLO 1
```

or a build define:

```text
COOPDX_SOLO=1
```

Avoid scattering ad hoc checks like `#ifdef NO_NETWORK` everywhere. The mode
should describe the product goal, not one implementation detail.

### 2.2 Gate Network Startup

In solo mode:

- do not initialize online networking
- do not start server/client discovery
- do not require account/session state
- do not show network errors
- do not poll remote state in normal gameplay

Prefer stubs that return "offline/single-player" over deleting functions in the
first pass. Deletion can happen after the build is stable.

### 2.3 Gate DJUI Entry Points

In solo mode:

- bypass coopdx main menu panels
- bypass server browser panels
- bypass player list/chat panels
- keep any DJUI code required by options only if it is not visible

The first implementation can leave DJUI compiled but unreachable. That reduces
blast radius while boot/menu/save work is being restored.

## Phase 3: Restore Vanilla Boot And Menu Flow

### 3.1 Restore Title Flow

Target behavior:

1. app launches
2. assets are available or importer runs
3. game shows vanilla intro/title behavior
4. player reaches vanilla-style file select

Likely reference areas:

- `deps/sm64ex/src/engine/level_script.c`
- `deps/sm64ex/src/menu/intro_geo.c`
- `deps/sm64ex/src/menu/file_select.c`
- `deps/sm64ex/src/menu/star_select.c`
- coopdx equivalents under `deps/sm64coopdx/src/`

Expected issues:

- coopdx may have a loading splash before game startup
- title state may be bypassed by DJUI
- intro assets may exist but be disabled
- input ownership may differ between DJUI and vanilla menus

### 3.2 Restore File Select

Goal:

- vanilla save slots
- copy/erase/select behavior
- score display
- star/course display
- no multiplayer lobby flow

Implementation guidance:

- first make file select compile and draw
- then wire it to actual save state
- then validate slot transitions and course entry

Do not begin by trying to make the UI perfect. First prove the flow:

```text
boot -> title -> file select -> castle grounds/castle -> save -> reboot -> load
```

### 3.3 Restore Pause And Options UX

Keep the in-game UX closer to sm64ex:

- pause menu behaves like vanilla/sm64ex
- video options expose fixed FPS choices
- audio/controller options remain available
- no network panels

If coopdx has useful enhanced options, hide them initially unless they are
needed for rendering validation.

## Phase 4: Save Management

This is the biggest product decision.

### Recommended Decision

Use vanilla sm64ex-style save slots for game progress, and keep launcher/config
state separate.

Rationale:

- matches the desired vanilla solo product
- avoids inheriting multiplayer save assumptions
- makes first-run/importer behavior easier to reason about
- simplifies regression testing

### 4.1 Save Format Policy

Define one of these explicitly:

1. **Vanilla format only**
   - simplest product
   - no coopdx save compatibility
   - best fit for a solo sm64ex-like app

2. **Vanilla format plus migration**
   - accepts existing coopdx saves
   - higher risk
   - only worth doing if existing user data matters

3. **Coopdx format behind vanilla UI**
   - fastest if coopdx save code is deeply coupled
   - may preserve hidden multiplayer fields
   - less clean long term

Recommended first milestone: vanilla format only.

### 4.2 Save Path Policy

Keep app data under a solo app namespace, not the coopdx namespace:

```text
~/Library/Application Support/sm64ex-macapp/
```

or a renamed final product namespace.

Do not silently read/write normal coopdx saves. That avoids corrupting a user's
existing coopdx installation.

### 4.3 Save Validation Cases

Validate:

- create new file
- collect one star
- save and quit
- relaunch
- file shows correct star count
- copy file
- erase file
- enter castle after loading
- pause-save behavior works
- no network/player metadata is required

## Phase 5: Fixed FPS Options

### Target UX

Match the current sm64ex patch:

```text
Target FPS: 30 / 60 / 120
```

Default:

```text
60 FPS
```

Valid values:

```text
30, 60, 120
```

### 5.1 Preserve Coopdx Rendering Ownership

Do not rewrite coopdx rendering to match the current sm64ex patch. Instead,
adapt the presentation frame scheduling around coopdx's existing renderer.

The FPS selector should control:

- presentation frames per 30 Hz simulation tick
- displayed target FPS
- config persistence

Mapping:

```text
30 FPS  -> 1 presentation frame per sim tick
60 FPS  -> 2 presentation frames per sim tick
120 FPS -> 4 presentation frames per sim tick
```

### 5.2 Preserve Title/Menu Skip Rules

Keep explicit skip behavior for flows that should not interpolate:

- title screen transitions
- reset/warp discontinuities
- camera snap events
- first frame after file load

Do not let 120 FPS expose uninitialized previous-state data during startup.

### 5.3 Config Integration

Add or adapt:

- `configTargetFPS`
- config validation to clamp invalid values back to `60`
- option text strings
- menu choice values
- presentation frame calculation

Reference current patch behavior in:

- `patches/selectable-fps.patch`
- `build/sm64ex/src/pc/pc_main.c`
- `build/sm64ex/src/game/options_menu.c`
- `build/sm64ex/src/pc/configfile.c`

Do not copy the sm64ex matrix/shadow patches into coopdx unless needed. Coopdx
already owns those systems.

## Phase 6: Gameplay Mod Removal

### 6.1 Remove In Layers

Do not delete all multiplayer code at once. Use this progression:

1. unreachable but compiled
2. compiled out in solo mode
3. source removed or isolated

This keeps the renderer stable while frontend/save work changes.

### 6.2 Systems To Gate First

High-priority gates:

- network initialization
- server browser
- player list
- chat
- multiplayer pause/menu panels
- remote player spawn/update
- online update checker
- Discord activity if it refers to coopdx multiplayer

Medium-priority gates:

- Lua/mod loader
- mod menu
- character selection
- palette/player customization
- coop-only debug panels

Low-priority cleanup:

- unused source files
- unused assets
- branding strings
- dead config entries

### 6.3 Vanilla Behavior Audit

After the solo path boots, compare against sm64ex for:

- Mario movement
- object behavior
- camera behavior
- course entry/exit
- star collection
- save prompts
- pause behavior
- death/game-over flow

Any gameplay behavior difference should be classified:

- inherited from coopdx and acceptable
- inherited from coopdx and must revert
- caused by solo-mode changes

## Phase 7: macOS App Packaging

### 7.1 Reuse This Repo's Launcher Model

Keep:

- native macOS wrapper
- first-run ROM prompt
- importer log
- external-data packaged runtime
- app bundle codesigning

Adapt paths and executable names:

```text
out/sm64ex.app/Contents/MacOS/sm64ex-bin
```

can remain initially, but final naming should be decided later.

### 7.2 Coopdx Install Flow Finding

Coopdx's default install/runtime flow differs from this repo's current sm64ex
flow.

Current sm64ex-macapp flow:

1. The native launcher asks for a user-owned ROM.
2. `scripts/import_rom.py` validates and normalizes it.
3. Assets are extracted into a generated external data pack.
4. The app writes:

   ```text
   ~/Library/Application Support/sm64ex-macapp/res/base.zip
   ```

5. The launcher starts `sm64ex-bin --datapath <support-dir>`.
6. The game reads external assets from `base.zip`.

Coopdx default flow:

1. The game initializes its filesystem with `--savepath` or `sys_user_path()`.
2. `main_rom_handler()` scans the write path and executable directory.
3. `rom_checker.cpp` accepts a vanilla US `.z64` ROM by MD5.
4. It copies the ROM into the write path as:

   ```text
   baserom.us.z64
   ```

5. `main_game_init()` calls `rom_assets_load()`.
6. `rom_assets.c` opens `gRomFilename` and fills queued `ROM_ASSET_LOAD_*`
   buffers directly from the ROM at runtime.

This means coopdx already has a runtime ROM-asset loader. It does not use the
sm64ex `EXTERNAL_DATA=1 NOEXTRACT=1 --datapath <dir>` model in the same way.

### 7.3 Recommended Solo First-Run Flow

For the first coopdx-solo spike, prefer the smallest flow that preserves
coopdx rendering:

1. Keep this repo's native Swift file picker and progress/error panel.
2. Reuse `scripts/prepare_rom.py` to accept `.z64`, `.n64`, and `.v64`.
3. Validate against the canonical US SHA-1.
4. Write the normalized ROM to the app support directory as:

   ```text
   baserom.us.z64
   ```

5. Write metadata beside it:

   ```text
   import.json
   importer.log
   ```

6. Launch coopdx-solo with:

   ```text
   --savepath <support-dir>
   --skip-update-check
   --no-discord
   --disable-mods
   ```

7. In solo mode, bypass coopdx's drag-and-drop ROM setup screen because the
   launcher has already prepared the ROM.

This is simpler than forcing coopdx into the current `base.zip` model. It lets
coopdx keep its `rom_assets_load()` path, which is part of the rendering/data
architecture already proven upstream.

### 7.4 ROM Copy vs Extracted Data Pack Decision

There is one important product/legal/UX decision:

- **Store normalized `baserom.us.z64` in app support**
  - aligns with coopdx's current runtime asset loader
  - simplest implementation
  - fastest path to a working coopdx-solo spike
  - stores the user's full ROM locally after first launch

- **Store only generated `base.zip` external data**
  - matches this repo's current sm64ex-macapp behavior
  - avoids keeping a full copied ROM in app support
  - requires porting or replacing coopdx's `rom_assets_load()` behavior
  - higher risk because many source files queue ROM assets by physical ROM
    offsets through `ROM_ASSET_LOAD_*`

Recommended first milestone: store the normalized ROM in the solo app support
directory and keep coopdx's loader intact.

Recommended later hardening: if avoiding a copied ROM is important, add a
second milestone to make `rom_assets_load()` read from a generated asset pack.
Do not do that before the coopdx-solo rendering path is proven.

### 7.5 First-Run Look And Feel

The first-run experience can still be fully custom even if the backend stores
`baserom.us.z64`.

Recommended UX:

- native macOS file picker, not coopdx's drag-and-drop loading screen
- short explanation that the ROM is required and must be user-owned
- accepts `.z64`, `.n64`, and `.v64`
- progress panel says "Preparing game data" or "Preparing game files"
- detailed importer log available on failure
- on success, launch directly into the solo game

This keeps the established first-run shape from this repo while avoiding
coopdx's multiplayer-branded loading/setup screen.

Implementation notes:

- Rename launcher strings away from `sm64ex` once the product name is chosen.
- For coopdx-solo, the launcher should check for:

  ```text
  <support-dir>/baserom.us.z64
  ```

  instead of:

  ```text
  <support-dir>/res/base.zip
  ```

- A coopdx-solo importer can be much smaller than `scripts/import_rom.py` at
  first because it only needs to normalize, validate, copy, and write metadata.
- If the final product later returns to an extracted data pack, keep the Swift
  UI and replace only the importer backend.

### 7.6 External Data Compatibility

Confirm coopdx can support the same ROM-free packaged asset model:

- build external data
- package importer template
- import user-owned ROM on first run
- write `base.zip` or equivalent data archive
- start game with explicit read-only datapath

If coopdx's asset pipeline differs, isolate that in the build script rather
than in game code.

## Phase 8: Rendering Regression Suite

The whole point of the coopdx base is stable enhanced rendering. Build the
validation plan around known failure modes.

### 8.1 Known High-FPS Cases

Test at 30, 60, and 120 FPS:

- Mario jumping in Bob-omb Battlefield
- fast direction changes on flat ground
- quick turn into jump
- long jump direction reversal
- camera snap/R-camera changes
- moving platforms
- shadows over slopes
- shadows over ice/carpet/water/lava
- file select/title transitions

### 8.2 Specific Shadow Checks

Confirm:

- shadow does not reuse one delta's geometry for later deltas
- shadow follows Mario smoothly at 60 FPS
- shadow follows Mario smoothly at 120 FPS
- shadow does not flicker during jump start/landing
- shadow does not lag behind on fast direction changes
- no obvious layer issue over ice/carpet/water

### 8.3 Matrix/Animation Checks

Confirm:

- Mario body does not jitter during rapid yaw changes
- held objects remain stable
- doors and animated objects do not smear or pop badly
- camera-space objects interpolate in world-correct fashion
- animation switches intentionally skip interpolation when needed

If rapid direction changes still glitch on coopdx, investigate state pairing:

- current display list with previous matrix from another state
- animation switch without interpolation skip
- graph-node switch case changing between frames
- previous animation frame from a different animation
- stale previous object transform after teleport/warp/snap

## Phase 9: Milestones

### Milestone A: Coopdx Builds In This Repo

Deliverables:

- pinned coopdx source
- disposable build tree
- successful macOS build
- no solo UX yet

Validation:

```sh
bash -n build-coopdx-solo.sh
./build-coopdx-solo.sh
codesign --verify --deep --strict --verbose=2 out/sm64ex.app
```

### Milestone B: Offline Solo Boot

Deliverables:

- network startup gated
- no server browser required
- game can enter a local playable state

Validation:

- app launches without network
- no network error UI
- no remote player state required

### Milestone C: Vanilla Frontend

Deliverables:

- vanilla title path
- vanilla-style file select
- file start enters game

Validation:

- boot/title/file-select smoke test
- controller and keyboard input work
- title/menu interpolation does not expose startup artifacts

### Milestone D: Save Management

Deliverables:

- vanilla save slots
- save/load/copy/erase
- isolated app save path

Validation:

- collect star
- save
- relaunch
- load same file
- copy/erase slots

### Milestone E: Fixed FPS Options

Deliverables:

- `Target FPS`
- `30 / 60 / 120 FPS` choices
- persisted config
- presentation frame scheduling wired to choice

Validation:

- 30 renders one presentation frame per sim tick
- 60 renders two
- 120 renders four
- invalid config value resets to 60
- title/menu skip behavior remains intact

### Milestone F: Rendering Stability Pass

Deliverables:

- documented high-FPS test matrix
- known shadow flicker cases pass
- known fast-direction cases classified or fixed

Validation:

- BOB jump shadow at 60 and 120 FPS
- rapid direction changes
- camera snap tests
- moving surfaces

### Milestone G: Cleanup

Deliverables:

- unreachable network UI removed or compiled out
- coopdx branding reduced or replaced where user-visible
- dead config hidden
- README updated

Validation:

- clean build from scratch
- no user-visible multiplayer affordances
- app still codesigns

## Risk Register

### Save Format Coupling

Risk:

Coopdx save code may be coupled to multiplayer/mod state.

Mitigation:

Prototype file select with stubbed save first. Decide save format early. Keep
config separate from game progress.

### Hidden Network Assumptions

Risk:

Gameplay or UI code may assume network/player systems exist even offline.

Mitigation:

Stub offline single-player state before deleting network systems.

### DJUI Input Ownership

Risk:

DJUI may capture input or own frontend flow in ways that conflict with vanilla
menus.

Mitigation:

Bypass DJUI entry points in solo mode. Leave compiled until vanilla menus are
stable.

### Rendering Dependency Damage

Risk:

Removing coopdx systems may accidentally remove rendering state, hooks, or skip
logic.

Mitigation:

Mark rendering files as protected. Gate frontend/network first. Do not delete
shared utility code until usage is audited.

### Gameplay Drift

Risk:

Coopdx has non-rendering gameplay changes that remain visible in solo mode.

Mitigation:

Run a vanilla behavior audit after boot/save works. Revert behavior differences
one subsystem at a time.

### Build Complexity

Risk:

Coopdx asset/build assumptions may not match this repo's ROM-free macOS app
packaging.

Mitigation:

Keep build-script changes isolated. First build coopdx normally, then adapt
external-data/importer packaging.

## Recommended First Spike

The first spike should answer one question:

Can this repo build and package a pinned coopdx executable without touching the
coopdx rendering path?

Scope:

1. Add coopdx lock entries.
2. Add `build-coopdx-solo.sh`.
3. Create `build/coopdx-solo` from `deps/sm64coopdx`.
4. Build the executable.
5. Package it with the existing launcher/importer as far as possible.
6. Do not restore menus yet.

Exit criteria:

- executable builds
- app bundle packages
- codesign passes
- remaining blocker list is about UX/save/frontend, not renderer/build basics

Only after that should the vanilla menu and save restoration begin.

## Recommended Technical Direction

Use coopdx as the engine/rendering base.

Port from sm64ex:

- vanilla title/menu flow
- file select UX
- save-slot behavior
- simple config/options presentation
- fixed target FPS option UX
- macOS wrapper/importer behavior from this repo

Keep from coopdx:

- rendering graph interpolation architecture
- shadow interpolation buffers
- per-delta regeneration behavior
- camera/viewport/skybox interpolation
- high-FPS rendering skip logic
- any low-level renderer fixes that are not multiplayer-specific

Gate or remove from coopdx:

- online networking
- server browser
- chat
- remote player sync
- multiplayer panels
- player customization if visible
- Lua/mod UI unless later desired

This direction has more upfront integration work than continuing sm64ex patch
ports, but it should reduce long-term rendering instability because the
rendering systems remain internally consistent.

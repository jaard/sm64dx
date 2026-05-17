# Repository Instructions

## Project Intent

This repo is a solo Super Mario 64 product spike built from the `sm64coopdx`
engine/rendering base. The goal is a mostly vanilla, single-player experience
closer to `sm64ex`, while preserving coopdx's high-FPS rendering stability.

Treat [PLAN.md](/Users/jaard/Projects/sm64coopdx-solo/PLAN.md) as the product
roadmap. The important architectural rule is to keep coopdx rendering systems
internally consistent and move solo behavior around them.

## Current Repository Shape

This checkout is currently a direct `sm64coopdx` source tree, not yet the future
`deps/sm64coopdx` plus patch-stack layout described in the plan.

Relevant paths:

- [Makefile](/Users/jaard/Projects/sm64coopdx-solo/Makefile): primary build
  entry point and macOS app-bundle recipe.
- [src/pc/pc_main.c](/Users/jaard/Projects/sm64coopdx-solo/src/pc/pc_main.c):
  startup, ROM handling call site, frame loop, interpolation frame scheduling.
- [src/pc/rom_checker.cpp](/Users/jaard/Projects/sm64coopdx-solo/src/pc/rom_checker.cpp):
  current ROM discovery/copy flow, writes `baserom.<version>.z64`.
- [src/pc/rom_assets.c](/Users/jaard/Projects/sm64coopdx-solo/src/pc/rom_assets.c):
  runtime ROM-backed asset loader. Do not replace this before the coopdx-solo
  rendering baseline is proven.
- [src/pc/configfile.c](/Users/jaard/Projects/sm64coopdx-solo/src/pc/configfile.c):
  persisted PC-port configuration.
- [src/pc/djui](/Users/jaard/Projects/sm64coopdx-solo/src/pc/djui):
  coopdx UI/frontend panels. Solo mode should first bypass unwanted panels,
  then compile out or remove code after the solo path is stable.
- [src/pc/network](/Users/jaard/Projects/sm64coopdx-solo/src/pc/network):
  networking and remote-player systems. Gate through a central solo mode, not
  scattered one-off feature flags.
- [src/menu](/Users/jaard/Projects/sm64coopdx-solo/src/menu),
  [levels/intro](/Users/jaard/Projects/sm64coopdx-solo/levels/intro), and
  [levels/menu](/Users/jaard/Projects/sm64coopdx-solo/levels/menu): vanilla
  title/menu/file-select areas to audit and restore.
- [src/game/save_file.c](/Users/jaard/Projects/sm64coopdx-solo/src/game/save_file.c):
  save-slot behavior and save-format work.
- [src/game/rendering_graph_node.c](/Users/jaard/Projects/sm64coopdx-solo/src/game/rendering_graph_node.c),
  [src/game/shadow.c](/Users/jaard/Projects/sm64coopdx-solo/src/game/shadow.c),
  and [src/game/skybox.c](/Users/jaard/Projects/sm64coopdx-solo/src/game/skybox.c):
  protected rendering/interpolation areas. Avoid touching these unless the task
  is specifically rendering validation or a targeted rendering bug fix.

## Implementation Rules

- Establish and preserve a clean macOS coopdx build before menu/save/frontend
  changes.
- Introduce one central solo build/config mode, for example `COOPDX_SOLO=1`.
  Avoid spreading unrelated flags such as `NO_NETWORK` across the codebase.
- Disable multiplayer in layers: first unreachable but compiled, then compiled
  out in solo mode, then removed after the solo path is proven.
- Prefer offline/single-player stubs over deleting network code in the first
  pass.
- Do not port sm64ex matrix, shadow, skybox, or interpolation patches into
  coopdx unless a direct coopdx rendering defect is proven. Coopdx owns those
  systems.
- Keep game progress saves separate from launcher/config state.
- Use a solo app-support namespace for saves and imported ROM data. Do not
  silently read or write a normal user's coopdx save/config directory.
- For the first macOS importer spike, prefer storing a validated normalized
  `baserom.us.z64` in the solo app-support directory and keeping
  `rom_assets_load()` intact. A `base.zip`/external-data replacement is a later
  hardening task.
- Avoid unrelated cleanup while the baseline is not buildable.

## Build And Verification

Use Homebrew GNU Make (`gmake`) on macOS. The Apple-provided `make` is too old
for this Makefile's GNU extensions.

The fast first compile should avoid app packaging and online SDK dylib copying:

```sh
gmake -j8 USE_APP=0 DISCORD_SDK=0 COOPNET=0
```

The current developer script,
[developer/compile.sh](/Users/jaard/Projects/sm64coopdx-solo/developer/compile.sh),
builds a development multiplayer run target and launches it with `--server`.
Do not use it as the solo baseline command.

For app-bundle work, inspect the Makefile bundle recipe first. It currently
unconditionally copies Discord, CoopNet, SDL2, and GLEW dylibs into
`build/us_pc/sm64coopdx.app`.

Before committing build-script edits, at minimum run:

```sh
bash -n <script>
```

When the bundle path exists, verify:

```sh
codesign --verify --deep --strict --verbose=2 build/us_pc/sm64coopdx.app
```

## First Milestone

The first milestone is not vanilla menus. It is a fresh macOS coopdx executable
build from this repo, with no rendering-path changes. After that works, add a
parallel solo build/config path and start gating network-first startup.

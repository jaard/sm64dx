# Where Is Mario?

Structured investigation plan for the `COOPDX_SOLO` gameplay bug where the
gameplay state works but Mario does not render.

## Goal

Find the first point where local Mario stops being renderable in solo offline
gameplay, then fix that point with a small `COOPDX_SOLO && gNetworkType ==
NT_NONE` change.

The target behavior is:

- Splash/title/Goddard/file select remain reachable.
- Starting a save enters gameplay.
- Mario renders normally in gameplay.
- Movement, camera, collision, swimming, climbing, and object interaction keep
  working.
- Non-solo coopdx host/client behavior remains unchanged.

## Constraints

- Do not route solo mode through normal coopdx host/server networking.
- Do not send network packets in `NT_NONE`.
- Do not broadly rewrite rendering, interpolation, matrix, shadow, or skybox
  code.
- Keep rendering systems internally consistent with coopdx.
- Prefer explicit solo stubs over deleting or bypassing large networking
  systems.
- Keep fixes reversible and tightly scoped.

## Current State

Known good:

- Old splash/title/Goddard intro works.
- Goddard hand/controller input works.
- File select works.
- A save can be started.
- Gameplay updates are running.
- Mario exists physically enough to move, swim, climb trees, and affect camera
  or state.

Known bad:

- Mario is invisible in actual gameplay.
- This is not currently known to be a crash.

Most likely class of bug:

- The local Mario object exists and updates, but its graph node, model,
  generated display lists, palette/model metadata, area visibility, or camera
  visibility path makes it skip rendering.

## Attempted Changes That Did Not Fix It

These are present in the current working tree or were recently attempted, but
manual smoke still reports Mario invisible:

- `src/pc/network/network_player.c`
  - `network_player_init_solo_local()` initializes player 0 as connected local
    metadata without calling `network_player_connected()`.
  - A later attempt added `memset(np, 0, sizeof(struct NetworkPlayer))`.
  - A later attempt added an `NT_NONE` early path in
    `network_player_update_course_level()` that marks level/area/position sync
    valid and `fadeOpacity = 32`.
- `src/game/level_update.c`
  - A later attempt preserves local `fadeOpacity = 32` during change-area and
    change-level transitions in solo offline mode.
- `src/pc/network/network.c`
  - A later attempt calls `network_player_update()` even in solo `NT_NONE`.

Do not spend another pass re-arguing these as the primary fix unless new
instrumentation proves they are incomplete in a specific way.

## Primary Questions

Answer these in order. Do not skip to speculative fixes before the facts are
known.

1. Does `gMarioStates[0].marioObj` exist in gameplay?
2. Is it the expected Mario behavior object?
3. Is the object graph node active, visible, and in the current render area?
4. Does the object have a valid shared child/model graph?
5. Is the object being rejected by `obj_is_in_view()`?
6. Is the object being hidden by first-person/camera logic?
7. Is the Mario geo running and producing display lists?
8. Are generated Mario colors/body state valid?
9. Is a bad local/global player index causing geo code to read the wrong
   `gNetworkPlayers[]`, `gBodyStates[]`, or `gMarioStates[]` slot?
10. What differs from normal coopdx host mode at the same point?

## Instrumentation Pass 1: Mario Object Snapshot

Add temporary logging after `init_single_mario()` finishes local player setup
and during the first few gameplay frames of `bhv_mario_update()`.

Log for player 0:

- `m`
- `m->playerIndex`
- `m->action`
- `m->marioObj`
- `m->marioObj->behavior == bhvMario`
- `m->marioObj->activeFlags`
- `m->marioObj->header.gfx.node.flags`
- decoded flags:
  - `GRAPH_RENDER_ACTIVE`
  - `GRAPH_RENDER_INVISIBLE`
  - `GRAPH_RENDER_HAS_ANIMATION`
  - `GRAPH_RENDER_PLAYER`
- `m->marioObj->header.gfx.areaIndex`
- `m->marioObj->header.gfx.activeAreaIndex`
- `gCurGraphNodeRoot->areaIndex` when rendering, if available
- `m->marioObj->header.gfx.sharedChild`
- `m->marioObj->header.gfx.sharedChild->type`, if non-null
- `m->marioObj->oBehParams`
- `m->marioObj->globalPlayerIndex`
- `gNetworkPlayers[0].connected`
- `gNetworkPlayers[0].type`
- `gNetworkPlayers[0].localIndex`
- `gNetworkPlayers[0].globalIndex`
- `gNetworkPlayers[0].overrideModelIndex`
- `gNetworkPlayers[0].currCourseNum`
- `gNetworkPlayers[0].currActNum`
- `gNetworkPlayers[0].currLevelNum`
- `gNetworkPlayers[0].currAreaIndex`
- `gNetworkPlayers[0].currLevelSyncValid`
- `gNetworkPlayers[0].currAreaSyncValid`
- `gNetworkPlayers[0].currPositionValid`
- `gNetworkPlayers[0].fadeOpacity`
- `m->fadeWarpOpacity`
- `gNetworkAreaLoaded`
- `gNetworkAreaSyncing`

Recommended temporary helper:

```c
#ifdef COOPDX_SOLO
static void solo_debug_mario_render_state(const char *where, struct MarioState *m) {
    static u32 sLastFrame = 0;
    if (gGlobalTimer == sLastFrame) { return; }
    sLastFrame = gGlobalTimer;
    if (gGlobalTimer > 120) { return; }
    ...
}
#endif
```

Keep logs throttled. The goal is a readable trace, not a flood.

Expected outcome:

- If `sharedChild == NULL`, focus on model loading / `obj_set_model()` /
  `dynos_model_get_geo()`.
- If `GRAPH_RENDER_INVISIBLE` is set, find the writer.
- If `GRAPH_RENDER_ACTIVE` is clear, find the writer.
- If `areaIndex` does not match the render root area, focus on area loading and
  warp/change-area code.
- If metadata indexes are wrong, focus on solo network-player bootstrap.

## Instrumentation Pass 2: Render Rejection Point

Add temporary logs in `src/game/rendering_graph_node.c`, only for
`COOPDX_SOLO && gNetworkType == NT_NONE && node == &gMarioStates[0].marioObj->header.gfx`.

Check:

- Whether `geo_process_object()` is called for local Mario.
- Whether it passes the area check:
  - `node->header.gfx.areaIndex == gCurGraphNodeRoot->areaIndex`
- Whether `obj_is_in_view()` is called.
- Whether `obj_is_in_view()` returns false because:
  - `GRAPH_RENDER_INVISIBLE`
  - frustum bounds
  - distance/culling values
- Whether `node->header.gfx.sharedChild` is non-null immediately before child
  graph processing.

Expected outcome:

- If `geo_process_object()` is never reached, the object is not in the render
  graph or is inactive.
- If area check fails, the Mario object is valid but assigned to the wrong area.
- If frustum check fails while camera sees the scene and Mario is at normal
  coordinates, inspect position/prev-position/camera-to-object state.
- If child processing gets a null or error model, inspect model setup.

## Instrumentation Pass 3: Mario Geo Functions

Add temporary logs to Mario-specific geo functions, gated to local player 0:

- `geo_get_processing_object_index()`
- `geo_get_processing_mario_index()`
- `geo_mario_set_player_colors()`
- `geo_mario_tilt_torso()`
- any generated node that gates Mario display lists

Log:

- `gCurGraphNodeProcessingObject`
- `gCurGraphNodeProcessingObject->behavior == bhvMario`
- `gCurGraphNodeProcessingObject->oBehParams`
- `gCurGraphNodeProcessingObject->globalPlayerIndex`
- returned local index
- selected `gNetworkPlayers[index].overrideModelIndex`
- whether display list allocation succeeds in
  `geo_mario_create_player_colors_dl()`

Expected outcome:

- If geo code resolves player 0, metadata is probably adequate.
- If it resolves another slot or falls back unexpectedly, fix
  local/global-index mapping or `globalPlayerIndex` initialization.
- If color display-list allocation fails, investigate display-list pool or
  generated layer setup.

## Instrumentation Pass 4: Compare With Host Mode

Build and run normal host mode enough to capture the same snapshots after
entering gameplay.

Compare:

- `gNetworkPlayers[0]` fields after `network_player_connected()`
- Mario object flags and area fields after `init_single_mario()`
- `sharedChild` pointer and graph node type
- `globalPlayerIndex`
- `oBehParams`
- first 120 gameplay frames of:
  - `GRAPH_RENDER_INVISIBLE`
  - `GRAPH_RENDER_ACTIVE`
  - `GRAPH_RENDER_PLAYER`
  - `fadeOpacity`
  - `wasNetworkVisible`

Do not copy host startup behavior wholesale. Use host mode only as a known-good
reference for the local object and render metadata.

## Candidate Fix Areas

Only choose one after instrumentation identifies the failure point.

### Missing Safe Part Of `network_player_connected()`

If solo metadata differs from host mode in a way that affects rendering, add a
small helper that initializes only safe local fields:

- no packets
- no popups
- no hooks
- no area requests
- no server pointer unless required by read-only code

Possible shape:

```c
#ifdef COOPDX_SOLO
static void network_player_apply_solo_local_visuals(struct NetworkPlayer *np) {
    ...
}
#endif
```

### Wrong Area Metadata

If Mario's graph node area does not match the active render root, fix the solo
area transition path near `load_mario_area()`, `change_area()`, or
`network_on_loaded_area()`.

Avoid rendering-side exceptions unless the area metadata is proven correct and
the renderer has a solo-specific stale check.

### Model/SharedChild Not Installed

If `sharedChild` is null or an error model:

- Check whether `dynos_model_get_geo(MODEL_MARIO)` is valid at gameplay entry.
- Check whether `obj_set_model()` runs after the Mario object exists.
- Check whether `dynos_actor_override()` replaces the shared child unexpectedly.
- Check whether config model selection points to a loaded but invalid coopdx
  character model.

Prefer forcing `CT_MARIO` in solo only as a diagnostic first, not as the final
fix, unless custom character selection is explicitly out of scope.

### Invisible/Inactive Flag Writer

If flags are wrong:

- Add a temporary watch helper around the small set of known writers:
  - `init_single_mario()`
  - `execute_mario_action()`
  - `mario_update_hitbox_and_cap_model()`
  - automatic/cutscene action paths that clear `GRAPH_RENDER_ACTIVE`
  - first-person/camera visibility paths
- Find the exact frame and function that flips the bit.
- Fix the source, not the symptom.

### First-Person Or Camera Hiding

If Mario renders only when camera/first-person state changes:

- Inspect `get_first_person_enabled()`
- Inspect `ACT_FIRST_PERSON`
- Inspect `gFirstPersonCamera.enabled`
- Inspect render-time shadow/body hiding checks

Do not remove first-person support globally. Gate solo startup state reset if
the title/menu path leaves stale camera state behind.

## Temporary Debugging Rules

- Prefix temporary logs with `SOLO_MARIO_DEBUG`.
- Gate all instrumentation with `#ifdef COOPDX_SOLO`.
- Keep logs frame-limited.
- Remove or compile out logs before finalizing unless a small permanent assert
  is justified.
- Do not leave noisy per-frame logs in the final patch.

## Build And Verification

Fast loose solo build:

```sh
gmake -j8 COOPDX_SOLO=1 USE_APP=0
```

Packaged solo app:

```sh
gmake COOPDX_SOLO=1 USE_APP=1
```

Codesign check:

```sh
codesign --verify --deep --strict --verbose=2 build/us_pc/sm64coopdx.app
```

Non-solo regression compile:

```sh
gmake -j8 USE_APP=0 DISCORD_SDK=0 COOPNET=0
```

Manual smoke:

1. Launch packaged app.
2. Pass splash/title/Goddard/Press Start/file select.
3. Start a save.
4. Confirm Mario renders in gameplay.
5. Confirm movement, swimming, tree climbing, camera, and scene rendering still
   work.

## Suggested Next Pass

1. Add Pass 1 and Pass 2 instrumentation only.
2. Build loose solo executable.
3. Run to gameplay and capture the first 120 gameplay frames of logs.
4. Compare those logs against one host-mode capture if the failure is not
   obvious.
5. Make one targeted fix based on the first proven bad invariant.
6. Remove temporary logs.
7. Run all build checks and manual smoke.

## Stop Conditions

Stop and reassess if:

- Mario object state is fully valid and render traversal reaches child geo with
  a valid model, but no pixels appear.
- The issue only reproduces with one configured character model or palette.
- The issue only reproduces after a specific title/menu/file-select transition.
- A fix requires touching matrix/interpolation/render core outside a narrow
  verified bug.

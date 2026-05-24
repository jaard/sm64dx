# Options Menu Layout Refactor Plan

## Goal

Redesign the solo options submenu layout to use a clearer Render96ex-style
two-column presentation:

- option labels on the left
- option values on the right
- classic SM64 pixel/glyph text, not higher-resolution Render96ex text
- existing darkened pause background only
- existing scrollbars are allowed
- no new panels, borders, row boxes, dividers, or value boxes
- main `OPTIONS` page remains centered for now

The actual pages, options, values, input behavior, save behavior, and existing
DJUI fallback actions must not change.

## References

- Current implementation: `src/game/options_menu.c`
- Existing darkened background: `shade_screen()` in `src/game/ingame_menu.c`
- Render96ex options menu reference: `../Render96ex/src/game/options_menu.c`
- Classic-glyph two-column reference: `mods/pause-menu-engine/menu_display.lua`

Render96ex is useful as a C-side structural reference for menu drawing,
scrolling, and simple text treatment. The pause-menu-engine mod is useful as a
layout reference for labels left and values right with subtle value arrows.
Neither reference should be copied wholesale.

## Current Baseline

The reverted `src/game/options_menu.c` is back to the centered layout:

- `solo_text_color()` converts ASCII to SM64 dialog glyphs and centers text.
- `solo_draw_menu()` draws both main menu and submenus using centered labels and
  centered values beneath them.
- `solo_draw_bind_option()` draws bind labels centered and bind values in three
  centered slots below.
- `solo_draw_box()` is used only for scrollbars.
- `shade_screen()` is called from `src/game/ingame_menu.c` when the solo options
  menu is open.

This is the right starting point.

## First Pass Scope

Only change visual layout code in `src/game/options_menu.c`.

Do not change:

- any `SoloOption` data arrays
- option order or page structure
- config variables
- input handling
- bind capture behavior
- action callbacks
- the darkened background path in `ingame_menu.c`
- rendering, shadow, skybox, or interpolation systems

## Layout Rules

### Main Menu

Keep the main `OPTIONS` page centered:

- title remains centered
- main options remain centered
- current left/right selection arrows may remain for the main menu only
- existing main menu row spacing can remain unchanged

### Submenus

For every non-main menu, use a two-column layout:

- left column: option label, left-aligned
- right column: current value, right-aligned
- selected state: text color only
- no selected row rectangle
- no panel rectangle
- no vertical divider
- no boxes around bind values

Suggested initial constants:

```c
#define SOLO_OPT_LABEL_X 40
#define SOLO_OPT_VALUE_RIGHT_X 280
#define SOLO_OPT_VALUE_LEFT_ARROW_X 202
#define SOLO_OPT_VALUE_RIGHT_ARROW_X 286
#define SOLO_OPT_SUBMENU_ROW_START 140
#define SOLO_OPT_SUBMENU_ROW_STEP 24
#define SOLO_OPT_SUBMENU_ROW_MIN 32
```

These are first-pass values. Tune after running the game and checking long
labels/values.

## Text Helpers

Keep `solo_text_color()` for centered text.

Add stack-buffer helpers for left and right alignment:

- `solo_text_left_color(x, y, ascii, r, g, b)`
- `solo_text_right_color(rightX, y, ascii, r, g, b)`
- small wrappers for enabled, selected, disabled label/value states

Avoid using `get_generic_ascii_string_width()` in the per-frame draw loop
because it allocates. Instead:

1. Convert ASCII into a local `u8 text[SOLO_OPT_BUF_SIZE]`.
2. Measure the converted text with `get_generic_dialog_width(text)`.
3. Print at `rightX - width`.

## Colors

Use text color to carry state:

- normal label: white
- selected label/value: light blue
- disabled label/value: gray
- enabled boolean value: green
- disabled boolean value: red
- normal non-boolean value: pale yellow or white

Define named constants or helper functions rather than repeating RGB triples
through the draw loop.

Suggested first-pass colors:

```c
#define SOLO_COLOR_WHITE_R 255
#define SOLO_COLOR_WHITE_G 255
#define SOLO_COLOR_WHITE_B 255
#define SOLO_COLOR_SELECTED_R 128
#define SOLO_COLOR_SELECTED_G 192
#define SOLO_COLOR_SELECTED_B 255
#define SOLO_COLOR_DISABLED_R 128
#define SOLO_COLOR_DISABLED_G 128
#define SOLO_COLOR_DISABLED_B 128
#define SOLO_COLOR_VALUE_R 255
#define SOLO_COLOR_VALUE_G 255
#define SOLO_COLOR_VALUE_B 128
#define SOLO_COLOR_ON_R 128
#define SOLO_COLOR_ON_G 255
#define SOLO_COLOR_ON_B 128
#define SOLO_COLOR_OFF_R 255
#define SOLO_COLOR_OFF_G 128
#define SOLO_COLOR_OFF_B 128
```

## Value Arrows

For adjustable options on submenu pages, draw subtle arrows around the value:

- draw `<` just to the left of the value area
- draw `>` just to the right of the value area
- use the same color family as the selected/value text
- only draw arrows for the currently selected row when the option can change
  left/right
- do not draw arrows for disabled options
- do not draw arrows for actions like `Player Options`, `Sound Options`, or
  `Restore Defaults`

Candidate helper:

```c
static bool solo_option_has_adjustable_value(const struct SoloOption *opt) {
    return opt->type == SOLO_OPT_BOOL
        || opt->type == SOLO_OPT_CHOICE
        || opt->type == SOLO_OPT_RANGE
        || opt->type == SOLO_OPT_BIND;
}
```

For `SOLO_OPT_SUBMENU`, optionally draw only a right-side `>` hint. Keep it
subtle and value-side, not as a row marker.

## Bind Rows

The controls page is the highest-risk layout case.

First-pass bind row target:

```text
A Button                 KEY1   KEY2   KEY3
```

Rules:

- label remains left-aligned with other option labels
- three bind values are drawn in fixed right-side columns
- no boxes around bind slots
- selected bind slot uses selected light-blue text
- non-selected bind slots use normal value color
- while binding, the active slot still shows `...`

If bind names overflow, prefer slightly tighter slot anchors over adding boxes.

## Scrolling

Keep the existing scrollbar behavior and original row direction:

```c
y = rowStart - rowStep * i + sCurrentMenu->scroll * rowStep;
```

Do not introduce a separate panel scissor rectangle. Keep the existing scissor
area unless visual testing proves it needs a small adjustment.

Because the first pass reduces submenu row height from the current bind-heavy
centered layout, use one visible-count policy:

- main menu: keep `SOLO_OPT_MAIN_VISIBLE_COUNT`
- submenus: start with the existing `SOLO_OPT_VISIBLE_COUNT`
- controls may keep its existing special row step if bind readability needs it

Only increase visible count after checking that all rows remain legible and do
not crowd the prompt or warning text.

## Implementation Steps

1. Add color constants and left/right text helpers near the existing text
   helpers.
2. Split drawing into `solo_draw_main_menu()` and `solo_draw_submenu_menu()`,
   with `solo_draw_menu()` dispatching based on `sCurrentMenu == &sMenuMain`.
3. Move the current centered drawing loop into `solo_draw_main_menu()` with no
   behavior changes.
4. Implement the submenu drawing loop with left labels and right values.
5. Implement value-side arrows for the selected adjustable submenu option.
6. Implement unboxed two-column bind row drawing.
7. Keep the MSAA restart warning and R prompt exactly where they are unless they
   visibly collide.
8. Build and inspect the menu in-game.

## Verification

Run:

```sh
gmake -j8 COOPDX_SOLO=1 USE_APP=0
```

Manual checks:

- main `OPTIONS` page is still centered
- no new boxes appear besides existing scrollbars
- darkened pause background still comes from `shade_screen()`
- labels and values align consistently on all submenus
- selected row uses light-blue text, not red
- boolean values use green/red
- selected adjustable value shows subtle value-side arrows
- disabled `Frame Limit` is gray and has no arrows
- bind rows fit without boxes
- active bind slot shows `...`
- long labels do not collide with values
- MSAA restart warning still appears
- R prompt still appears and remains readable

## First-Pass Implementation Prompt

```text
Implement the first pass of the solo options menu layout refactor described in menu-refactor-plan.md.

Work only in src/game/options_menu.c unless a compile error proves another file needs a declaration. Keep all menu data, options, callbacks, config writes, and input behavior unchanged.

The main OPTIONS page must remain centered. Non-main option pages should switch to a two-column layout: labels left-aligned, values right-aligned, classic SM64 pixel text, no new panels, no row highlight boxes, no dividers, and no bind-slot boxes. Keep the existing darkened background path and existing scrollbars.

Use light blue for selected text instead of the current red selection color. Keep disabled text gray. Use green/red for boolean ON/OFF values. Add subtle < and > glyphs around the value area for the currently selected adjustable option only. For submenu entries, a subtle right-side > hint is acceptable. Do not use left/right arrows as row markers.

Avoid per-frame heap allocation for right-aligned text measurement: convert into a stack buffer and measure the converted SM64 string. Preserve bind capture behavior, including ... for the active binding slot, but draw bind values as unboxed fixed right-side columns.

After editing, run:
gmake -j8 COOPDX_SOLO=1 USE_APP=0

Report the files changed, the build result, and any visual-risk notes for manual in-game inspection.
```

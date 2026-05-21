#ifdef COOPDX_SOLO

#include <stdio.h>
#include <string.h>
#include <ultra64.h>

#include "audio/external.h"
#include "game/bettercamera.h"
#include "game/camera.h"
#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/level_info.h"
#include "game/player_palette.h"
#include "game/segment2.h"
#include "pc/configfile.h"
#include "pc/controller/controller_api.h"
#include "pc/controller/controller_bind_mapping.h"
#include "pc/djui/djui_panel_controls.h"
#include "pc/djui/djui_panel_pause.h"
#include "pc/djui/djui_panel_player.h"
#include "pc/djui/djui_panel_sound.h"
#include "pc/fs/fs.h"
#include "pc/gfx/gfx_window_manager_api.h"
#include "pc/lua/utils/smlua_audio_utils.h"
#include "pc/network/network_player.h"
#include "pc/pc_main.h"
#include "pc/platform.h"
#include "sm64.h"
#include "types.h"

#define SOLO_OPT_VISIBLE_COUNT 4
#define SOLO_OPT_MAIN_VISIBLE_COUNT 6
#define SOLO_OPT_BUF_SIZE 64
#define SOLO_MSAA_ORIGINAL_UNSET ((u32)-1)

enum SoloOptionType {
    SOLO_OPT_SUBMENU,
    SOLO_OPT_BOOL,
    SOLO_OPT_CHOICE,
    SOLO_OPT_RANGE,
    SOLO_OPT_BIND,
    SOLO_OPT_ACTION,
    SOLO_OPT_BACK,
};

struct SoloOption;
struct SoloMenu;

struct SoloChoice {
    const char *label;
    u32 value;
};

struct SoloDefaultBind {
    u32 *target;
    u32 values[MAX_BINDS];
};

typedef void (*SoloActionFn)(void);
typedef void (*SoloPanelCreateFn)(struct DjuiBase *caller);
typedef void (*SoloApplyFn)(void);
typedef void (*SoloChangeFn)(const struct SoloOption *opt, s32 dir);
typedef const char *(*SoloValueFn)(const struct SoloOption *opt, char *buf, size_t size);
typedef bool (*SoloEnabledFn)(const struct SoloOption *opt);

struct SoloOption {
    const char *label;
    enum SoloOptionType type;
    u32 *uval;
    bool *bval;
    s32 min;
    s32 max;
    s32 step;
    const struct SoloChoice *choices;
    u8 choiceCount;
    const struct SoloMenu *submenu;
    SoloActionFn action;
    SoloApplyFn apply;
    SoloChangeFn change;
    SoloValueFn valueText;
    SoloEnabledFn enabled;
};

struct SoloMenu {
    const char *title;
    const struct SoloOption *opts;
    s8 optCount;
    s8 select;
    s8 scroll;
    const struct SoloMenu *parent;
};

static bool sSoloOptionsOpen = false;
static s32 sInputTimer = 0;
static u8 sHoldCount = 0;
static const struct SoloOption *sBindingOption = NULL;
static u8 sBindIndex = 0;
static u32 sMsaaOriginal = SOLO_MSAA_ORIGINAL_UNSET;
static u32 sPalettePresetIndex = 0;
static bool sPalettesLoaded = false;

static struct SoloMenu sMenuMain;
static struct SoloMenu sMenuPlayer;
static struct SoloMenu sMenuCamera;
static struct SoloMenu sMenuFreeCamera;
static struct SoloMenu sMenuRomhackCamera;
static struct SoloMenu sMenuControls;
static struct SoloMenu sMenuDisplay;
static struct SoloMenu sMenuSound;

static const struct SoloMenu *sCurrentMenu = &sMenuMain;

static const struct SoloChoice sBoolChoices[] = {
    { "OFF", 0 },
    { "ON",  1 },
};

static const struct SoloChoice sRomhackCameraChoices[] = {
    { "AUTO", 0 },
    { "ON",   1 },
    { "OFF",  2 },
};

static const struct SoloChoice sFramerateChoices[] = {
    { "AUTO",     RRM_AUTO },
    { "MANUAL",   RRM_MANUAL },
    { "UNCAPPED", RRM_UNLIMITED },
};

static const struct SoloChoice sFrameLimitChoices[] = {
    { "30",  30 },
    { "60",  60 },
    { "90",  90 },
    { "120", 120 },
    { "180", 180 },
    { "240", 240 },
};

static const struct SoloChoice sInterpolationChoices[] = {
    { "FAST",     0 },
    { "ACCURATE", 1 },
};

static const struct SoloChoice sFilteringChoices[] = {
    { "NEAREST",  0 },
    { "LINEAR",   1 },
    { "TRIPOINT", 2 },
};

static const struct SoloChoice sDrawDistanceChoices[] = {
    { "0.5X",  0 },
    { "1X",    1 },
    { "1.5X",  2 },
    { "3X",    3 },
    { "10X",   4 },
    { "100X",  5 },
};

static const struct SoloDefaultBind sDefaultN64Binds[] = {
    { configKeyA,          { 0x0026, 0x1000, 0x1103 } },
    { configKeyB,          { 0x0033, 0x1001, 0x1101 } },
    { configKeyStart,      { 0x0039, 0x1006, VK_INVALID } },
    { configKeyL,          { 0x002A, 0x1009, 0x1104 } },
    { configKeyR,          { 0x0036, 0x100A, 0x101B } },
    { configKeyZ,          { 0x0025, 0x1007, 0x101A } },
    { configKeyCUp,        { 0x0148, VK_INVALID, VK_INVALID } },
    { configKeyCDown,      { 0x0150, VK_INVALID, VK_INVALID } },
    { configKeyCLeft,      { 0x014B, VK_INVALID, VK_INVALID } },
    { configKeyCRight,     { 0x014D, VK_INVALID, VK_INVALID } },
    { configKeyStickUp,    { 0x0011, VK_INVALID, VK_INVALID } },
    { configKeyStickDown,  { 0x001F, VK_INVALID, VK_INVALID } },
    { configKeyStickLeft,  { 0x001E, VK_INVALID, VK_INVALID } },
    { configKeyStickRight, { 0x0020, VK_INVALID, VK_INVALID } },
};

static const struct PlayerPalette sDefaultCharacterPalettes[CT_MAX] = {
    [CT_MARIO] =
        { { { 0x00, 0x00, 0xff }, { 0xff, 0x00, 0x00 }, { 0xff, 0xff, 0xff }, { 0x72, 0x1c, 0x0e }, { 0x73, 0x06, 0x00 }, { 0xfe, 0xc1, 0x79 }, { 0xff, 0x00, 0x00 }, { 0xff, 0x00, 0x00 } } },
    [CT_LUIGI] =
        { { { 0x00, 0x00, 0xff }, { 0x00, 0xff, 0x00 }, { 0xff, 0xff, 0xff }, { 0x72, 0x1c, 0x0e }, { 0x73, 0x06, 0x00 }, { 0xfe, 0xc1, 0x79 }, { 0x00, 0xff, 0x00 }, { 0x00, 0xff, 0x00 } } },
    [CT_TOAD] =
        { { { 0xff, 0xff, 0xff }, { 0x4c, 0x2c, 0xd3 }, { 0xff, 0xff, 0xff }, { 0x68, 0x40, 0x1b }, { 0x73, 0x06, 0x00 }, { 0xfe, 0xd5, 0xa1 }, { 0xff, 0x00, 0x00 }, { 0xff, 0x00, 0x00 } } },
    [CT_WALUIGI] =
        { { { 0x16, 0x16, 0x27 }, { 0x61, 0x26, 0xb0 }, { 0xff, 0xff, 0xff }, { 0xfe, 0x76, 0x00 }, { 0x73, 0x53, 0x00 }, { 0xfe, 0xc1, 0x79 }, { 0x61, 0x26, 0xb0 }, { 0xff, 0xde, 0x00 } } },
    [CT_WARIO] =
        { { { 0x7f, 0x20, 0x7a }, { 0xff, 0xbd, 0x00 }, { 0xff, 0xff, 0xff }, { 0x0e, 0x72, 0x1c }, { 0x73, 0x53, 0x00 }, { 0xfe, 0xc1, 0x79 }, { 0xff, 0xbd, 0x00 }, { 0x00, 0x00, 0xff } } },
};

static void solo_menu_close(void) {
    configfile_save(configfile_name());
    sSoloOptionsOpen = false;
    sCurrentMenu = &sMenuMain;
    sBindingOption = NULL;
    sInputTimer = 0;
    sHoldCount = 0;
}

static void solo_menu_open(void) {
    play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
    sSoloOptionsOpen = true;
    sCurrentMenu = &sMenuMain;
    sInputTimer = 0;
    sHoldCount = 0;
    if (sMsaaOriginal == SOLO_MSAA_ORIGINAL_UNSET) { sMsaaOriginal = configWindow.msaa; }
}

bool optmenu_is_open(void) {
    return sSoloOptionsOpen;
}

static void solo_display_apply(void) {
    configWindow.settings_changed = true;
}

static void solo_camera_apply(void) {
    newcam_init_settings();
    romhack_camera_init_settings();
}

static void solo_sound_apply(void) {
    audio_custom_update_volume();
}

static bool solo_frame_limit_enabled(UNUSED const struct SoloOption *opt) {
    return configFramerateMode == RRM_MANUAL;
}

static void solo_player_apply(void) {
    if (configPlayerModel >= CT_MAX) { configPlayerModel = CT_MARIO; }
    if (gNetworkPlayers[0].overrideModelIndex == gNetworkPlayers[0].modelIndex) {
        gNetworkPlayers[0].overrideModelIndex = configPlayerModel;
    }
    gNetworkPlayers[0].modelIndex = configPlayerModel;
    network_player_update_model(0);
}

static void solo_player_load_palettes(void) {
    if (sPalettesLoaded) { return; }

    player_palettes_reset();
    player_palettes_read(sys_resource_path(), true);
    player_palettes_read(fs_get_write_path(PALETTES_DIRECTORY), false);
    sPalettesLoaded = true;
}

static u32 solo_player_palette_index(struct PlayerPalette palette) {
    for (u32 i = 0; i < gPresetPaletteCount; i++) {
        if (memcmp(&palette, &gPresetPalettes[i].palette, sizeof(struct PlayerPalette)) == 0) {
            return i + 1;
        }
    }
    return 0;
}

static void solo_player_apply_palette(struct PlayerPalette palette) {
    if (memcmp(&gNetworkPlayers[0].overridePalette, &gNetworkPlayers[0].palette, sizeof(struct PlayerPalette)) == 0) {
        gNetworkPlayers[0].overridePalette = palette;
    }

    configPlayerPalette = palette;
    gNetworkPlayers[0].palette = configPlayerPalette;
}

static void solo_player_apply_default_palette(void) {
    if (configPlayerModel >= CT_MAX) { configPlayerModel = CT_MARIO; }

    solo_player_load_palettes();
    solo_player_apply_palette(sDefaultCharacterPalettes[configPlayerModel]);
    sPalettePresetIndex = solo_player_palette_index(configPlayerPalette);
}

static void solo_open_djui(SoloPanelCreateFn createPanel) {
    solo_menu_close();
    djui_panel_pause_create(NULL);
    createPanel(NULL);
}

static void solo_open_player_panel(void) { solo_open_djui(djui_panel_player_create); }
static void solo_open_controls_panel(void) { solo_open_djui(djui_panel_controls_create); }
static void solo_open_sound_panel(void) { solo_open_djui(djui_panel_sound_create); }

static void solo_restore_default_binds(void) {
    for (u8 i = 0; i < ARRAY_COUNT(sDefaultN64Binds); i++) {
        memcpy(sDefaultN64Binds[i].target, sDefaultN64Binds[i].values, sizeof(sDefaultN64Binds[i].values));
    }

    configStickDeadzone = 16;
    configRumbleStrength = 50;
    sBindingOption = NULL;
    sBindIndex = 0;
    controller_reconfigure();
}

static void solo_exit_game(void) {
    configfile_save(configfile_name());
    game_exit();
}

static const char *solo_bool_value(const struct SoloOption *opt, UNUSED char *buf, UNUSED size_t size) {
    return *opt->bval ? sBoolChoices[1].label : sBoolChoices[0].label;
}

static const char *solo_choice_value(const struct SoloOption *opt, char *buf, size_t size) {
    for (u8 i = 0; i < opt->choiceCount; i++) {
        if (*opt->uval == opt->choices[i].value) {
            return opt->choices[i].label;
        }
    }

    snprintf(buf, size, "%u", *opt->uval);
    return buf;
}

static const char *solo_range_value(const struct SoloOption *opt, char *buf, size_t size) {
    snprintf(buf, size, "%u", *opt->uval);
    return buf;
}

static const char *solo_bind_value(const struct SoloOption *opt, char *buf, size_t size) {
    if (sBindingOption == opt) {
        return "PRESS KEY";
    }

    snprintf(buf, size, "%u: %s", sBindIndex + 1, translate_bind_to_name(opt->uval[sBindIndex]));
    return buf;
}

static const char *solo_character_value(UNUSED const struct SoloOption *opt, UNUSED char *buf, UNUSED size_t size) {
    if (configPlayerModel >= CT_MAX) { configPlayerModel = CT_MARIO; }
    return gCharacters[configPlayerModel].name;
}

static const char *solo_palette_value(UNUSED const struct SoloOption *opt, UNUSED char *buf, UNUSED size_t size) {
    solo_player_load_palettes();
    sPalettePresetIndex = solo_player_palette_index(configPlayerPalette);
    if (sPalettePresetIndex == 0 || sPalettePresetIndex > gPresetPaletteCount) {
        return "CUSTOM";
    }

    return gPresetPalettes[sPalettePresetIndex - 1].name;
}

static const char *solo_msaa_value(UNUSED const struct SoloOption *opt, char *buf, size_t size) {
    if (configWindow.msaa == 0) { return "OFF"; }
    snprintf(buf, size, "%uX", configWindow.msaa);
    return buf;
}

static void solo_change_bool(const struct SoloOption *opt, UNUSED s32 dir) {
    *opt->bval = !*opt->bval;
    if (opt->apply) { opt->apply(); }
}

static void solo_change_choice(const struct SoloOption *opt, s32 dir) {
    s32 choice = 0;
    for (u8 i = 0; i < opt->choiceCount; i++) {
        if (*opt->uval == opt->choices[i].value) {
            choice = i;
            break;
        }
    }

    choice += dir;
    if (choice < 0) {
        choice = opt->choiceCount - 1;
    } else if (choice >= opt->choiceCount) {
        choice = 0;
    }

    *opt->uval = opt->choices[choice].value;
    if (opt->apply) { opt->apply(); }
}

static void solo_change_range(const struct SoloOption *opt, s32 dir) {
    s32 value = *opt->uval + opt->step * dir;
    if (value < opt->min) {
        value = opt->max;
    } else if (value > opt->max) {
        value = opt->min;
    }

    *opt->uval = value;
    if (opt->apply) { opt->apply(); }
}

static void solo_change_character(const struct SoloOption *opt, s32 dir) {
    s32 value = configPlayerModel + dir;
    if (value < 0) {
        value = CT_MAX - 1;
    } else if (value >= CT_MAX) {
        value = CT_MARIO;
    }

    configPlayerModel = value;
    solo_player_apply_default_palette();
    if (opt->apply) { opt->apply(); }
}

static void solo_change_palette(UNUSED const struct SoloOption *opt, s32 dir) {
    solo_player_load_palettes();
    if (gPresetPaletteCount == 0) { return; }

    s32 index = solo_player_palette_index(configPlayerPalette) + dir;
    if (index < 1) {
        index = gPresetPaletteCount;
    } else if (index > gPresetPaletteCount) {
        index = 1;
    }

    sPalettePresetIndex = index;
    solo_player_apply_palette(gPresetPalettes[sPalettePresetIndex - 1].palette);
}

static void solo_change_msaa(const struct SoloOption *opt, s32 dir) {
    const u32 values[] = { 0, 2, 4, 8, 16 };
    u8 count = 1;
    int maxMsaa = wm_api->get_max_msaa();
    if (maxMsaa >= 2) { count = 2; }
    if (maxMsaa >= 4) { count = 3; }
    if (maxMsaa >= 8) { count = 4; }
    if (maxMsaa >= 16) { count = 5; }

    s32 index = 0;
    for (u8 i = 0; i < count; i++) {
        if (configWindow.msaa == values[i]) {
            index = i;
            break;
        }
    }

    index += dir;
    if (index < 0) {
        index = count - 1;
    } else if (index >= count) {
        index = 0;
    }

    configWindow.msaa = values[index];
    if (opt->apply) { opt->apply(); }
}

static void solo_change_bind_slot(UNUSED const struct SoloOption *opt, s32 dir) {
    s32 index = sBindIndex + dir;
    if (index < 0) {
        index = MAX_BINDS - 1;
    } else if (index >= MAX_BINDS) {
        index = 0;
    }

    sBindIndex = index;
}

static void solo_change_option(const struct SoloOption *opt, s32 dir) {
    if (opt->enabled && !opt->enabled(opt)) { return; }

    if (opt->change) {
        opt->change(opt, dir);
        return;
    }

    switch (opt->type) {
        case SOLO_OPT_BOOL:   solo_change_bool(opt, dir);   break;
        case SOLO_OPT_CHOICE: solo_change_choice(opt, dir); break;
        case SOLO_OPT_RANGE:  solo_change_range(opt, dir);  break;
        case SOLO_OPT_BIND:   solo_change_bind_slot(opt, dir); break;
        default: break;
    }
}

static const char *solo_option_value(const struct SoloOption *opt, char *buf, size_t size) {
    if (opt->valueText) {
        return opt->valueText(opt, buf, size);
    }

    switch (opt->type) {
        case SOLO_OPT_BOOL:   return solo_bool_value(opt, buf, size);
        case SOLO_OPT_CHOICE: return solo_choice_value(opt, buf, size);
        case SOLO_OPT_RANGE:  return solo_range_value(opt, buf, size);
        case SOLO_OPT_BIND:   return solo_bind_value(opt, buf, size);
        default: return NULL;
    }
}

static void solo_action_back(void) {
    if (sCurrentMenu->parent != NULL) {
        sCurrentMenu = sCurrentMenu->parent;
    } else {
        solo_menu_close();
    }
}

static void solo_enter_option(const struct SoloOption *opt) {
    if (opt->enabled && !opt->enabled(opt)) { return; }

    switch (opt->type) {
        case SOLO_OPT_SUBMENU:
            sCurrentMenu = opt->submenu;
            break;
        case SOLO_OPT_ACTION:
            opt->action();
            break;
        case SOLO_OPT_BIND:
            sBindingOption = opt;
            controller_get_raw_key();
            break;
        case SOLO_OPT_BACK:
            solo_action_back();
            break;
        default:
            solo_change_option(opt, 1);
            break;
    }
}

static void solo_draw_box(s16 x1, s16 y1, s16 x2, s16 y2, u8 r, u8 g, u8 b) {
    gDPPipeSync(gDisplayListHead++);
    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCycleType(gDisplayListHead++, G_CYC_FILL);
    gDPSetFillColor(gDisplayListHead++, GPACK_RGBA5551(r, g, b, 255));
    gDPFillRectangle(gDisplayListHead++, x1, y1, x2 - 1, y2 - 1);
    gDPPipeSync(gDisplayListHead++);
    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
}

static void solo_text_color(s16 x, s16 y, const char *ascii, u8 r, u8 g, u8 b) {
    u8 text[SOLO_OPT_BUF_SIZE] = { DIALOG_CHAR_TERMINATOR };
    convert_string_ascii_to_sm64(text, ascii, false);

    s16 textX = get_str_x_pos_from_center(x, text, 10.0f);
    gDPSetEnvColor(gDisplayListHead++, 0, 0, 0, 255);
    print_generic_string(textX + 1, y - 1, text);
    gDPSetEnvColor(gDisplayListHead++, r, g, b, 255);
    print_generic_string(textX, y, text);
}

static void solo_text(s16 x, s16 y, const char *ascii, bool selected) {
    solo_text_color(x, y, ascii, 255, selected ? 32 : 255, selected ? 32 : 255);
}

static void solo_text_disabled(s16 x, s16 y, const char *ascii) {
    solo_text_color(x, y, ascii, 128, 128, 128);
}

static void solo_format_bind_value(u32 bind, char *buf, size_t size) {
    const char *value = bind == VK_INVALID ? "NONE" : translate_bind_to_name(bind);
    bool stripBrackets = value[0] == '[' && value[strlen(value) - 1] == ']';
    size_t start = stripBrackets ? 1 : 0;
    size_t end = stripBrackets ? strlen(value) - 1 : strlen(value);
    size_t dst = 0;

    for (size_t src = start; src < end && dst + 1 < size; src++) {
        char c = value[src];
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }
        buf[dst++] = c;
    }
    buf[dst] = '\0';
}

static void solo_draw_bind_option(const struct SoloOption *opt, s16 y, bool selected) {
    static const s16 sBindSlotX[] = { 112, 160, 208 };
    char valueBuf[SOLO_OPT_BUF_SIZE];

    solo_text(160, y, opt->label, selected);
    for (u8 i = 0; i < ARRAY_COUNT(sBindSlotX); i++) {
        if (sBindingOption == opt && sBindIndex == i) {
            snprintf(valueBuf, sizeof(valueBuf), "...");
        } else {
            solo_format_bind_value(opt->uval[i], valueBuf, sizeof(valueBuf));
        }

        solo_text(sBindSlotX[i], y - 13, valueBuf, selected && sBindIndex == i);
    }
}

static s16 solo_hud_centered_x(s16 x, const char *ascii) {
    s16 len = 0;
    for (const char *c = ascii; *c != '\0'; c++) {
        len += (*c == ' ') ? 6 : 12;
    }
    return x - len / 2;
}

static void solo_draw_title(void) {
    u8 title[SOLO_OPT_BUF_SIZE] = { GLOBAR_CHAR_TERMINATOR };
    convert_string_ascii_to_sm64(title, sCurrentMenu->title, false);

    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
    print_hud_lut_string(HUD_LUT_GLOBAL, solo_hud_centered_x(160, sCurrentMenu->title), 40, title);
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_end);
}

static void solo_draw_prompt(void) {
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    solo_text(264, 212, sSoloOptionsOpen ? "[R] Return" : "[R] Options", false);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

static s8 solo_menu_visible_count(const struct SoloMenu *menu) {
    return menu == &sMenuMain ? SOLO_OPT_MAIN_VISIBLE_COUNT : SOLO_OPT_VISIBLE_COUNT;
}

static s16 solo_menu_row_start(const struct SoloMenu *menu) {
    return menu == &sMenuMain ? 150 : 140;
}

static s16 solo_menu_row_step(const struct SoloMenu *menu) {
    return menu == &sMenuMain ? 24 : 32;
}

static s16 solo_menu_row_min(const struct SoloMenu *menu) {
    return menu == &sMenuMain ? 20 : 32;
}

static void solo_draw_menu(void) {
    solo_draw_title();

    s8 visibleCount = solo_menu_visible_count(sCurrentMenu);
    s16 rowStart = solo_menu_row_start(sCurrentMenu);
    s16 rowStep = solo_menu_row_step(sCurrentMenu);
    s16 rowMin = solo_menu_row_min(sCurrentMenu);

    if (sCurrentMenu->optCount > visibleCount) {
        s16 scrollpos = 54 * ((f32)sCurrentMenu->scroll / (sCurrentMenu->optCount - visibleCount));
        solo_draw_box(272, 90, 280, 208, 0x80, 0x80, 0x80);
        solo_draw_box(272, 90 + scrollpos, 280, 154 + scrollpos, 0xFF, 0xFF, 0xFF);
    }

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, 80, SCREEN_WIDTH, SCREEN_HEIGHT);

    for (s8 i = 0; i < sCurrentMenu->optCount; i++) {
        s16 y = rowStart - rowStep * i + sCurrentMenu->scroll * rowStep;
        if (y > rowStart || y <= rowMin) { continue; }

        const struct SoloOption *opt = &sCurrentMenu->opts[i];
        bool selected = sCurrentMenu->select == i;
        bool enabled = opt->enabled == NULL || opt->enabled(opt);
        s16 labelY = (opt->type == SOLO_OPT_SUBMENU || opt->type == SOLO_OPT_ACTION || opt->type == SOLO_OPT_BACK) ? y - 6 : y;
        char valueBuf[SOLO_OPT_BUF_SIZE];
        const char *value = solo_option_value(opt, valueBuf, sizeof(valueBuf));

        if (opt->type == SOLO_OPT_BIND) {
            solo_draw_bind_option(opt, y, selected);
        } else if (!enabled) {
            solo_text_disabled(160, labelY, opt->label);
        } else {
            solo_text(160, labelY, opt->label, selected);
        }
        if (value != NULL && opt->type != SOLO_OPT_BIND) {
            if (enabled) {
                solo_text(160, y - 13, value, selected);
            } else {
                solo_text_disabled(160, y - 13, value);
            }
        }
    }

    gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (sCurrentMenu->optCount > 0) {
        s16 arrowY = rowStart - 8 - rowStep * (sCurrentMenu->select - sCurrentMenu->scroll);
        solo_text(72, arrowY, "<", false);
        solo_text(232, arrowY, ">", false);
    }
    if (sCurrentMenu == &sMenuDisplay && sMsaaOriginal != SOLO_MSAA_ORIGINAL_UNSET && sMsaaOriginal != configWindow.msaa) {
        solo_text_color(160, 12, "Restart the game to apply changes.", 255, 160, 0);
    }

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
    solo_draw_prompt();
}

static void solo_move_selection(s32 dir) {
    struct SoloMenu *menu = (struct SoloMenu *)sCurrentMenu;
    s8 visibleCount = solo_menu_visible_count(menu);

    menu->select += dir;
    if (menu->select < 0) {
        menu->select = menu->optCount - 1;
    } else if (menu->select >= menu->optCount) {
        menu->select = 0;
    }

    if (menu->select < menu->scroll) {
        menu->scroll = menu->select;
    } else if (menu->select > menu->scroll + visibleCount - 1) {
        menu->scroll = menu->select - (visibleCount - 1);
    }
}

static bool solo_allow_input(void) {
    sInputTimer--;
    if (sInputTimer <= 0) {
        if (sHoldCount == 0) {
            sHoldCount++;
            sInputTimer = 10;
        } else {
            sInputTimer = 3;
        }
        return true;
    }
    return false;
}

static void solo_reset_repeat(void) {
    sHoldCount = 0;
    sInputTimer = 0;
}

static void solo_update_open_menu(void) {
    if (sBindingOption != NULL) {
        u32 key = controller_get_raw_key();
        if (key != VK_INVALID) {
            sBindingOption->uval[sBindIndex] = key;
            sBindingOption = NULL;
            controller_reconfigure();
            play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
        }
        return;
    }

    if (gPlayer1Controller->buttonPressed & R_TRIG) {
        play_sound(SOUND_MENU_MARIO_CASTLE_WARP2, gGlobalSoundSource);
        solo_menu_close();
        return;
    }

    bool allowInput = solo_allow_input();

    if (ABS(gPlayer1Controller->stickY) > 60) {
        if (allowInput) {
            play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
            solo_move_selection(gPlayer1Controller->stickY >= 60 ? -1 : 1);
        }
    } else if (ABS(gPlayer1Controller->stickX) > 60) {
        if (allowInput) {
            play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
            solo_change_option(&sCurrentMenu->opts[sCurrentMenu->select], gPlayer1Controller->stickX >= 60 ? 1 : -1);
        }
    } else if (gPlayer1Controller->buttonPressed & A_BUTTON) {
        if (allowInput) {
            play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
            solo_enter_option(&sCurrentMenu->opts[sCurrentMenu->select]);
        }
    } else if (gPlayer1Controller->buttonPressed & B_BUTTON) {
        if (allowInput) {
            play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
            solo_action_back();
        }
    } else if (gPlayer1Controller->buttonPressed & START_BUTTON) {
        if (allowInput) {
            play_sound(SOUND_MENU_MARIO_CASTLE_WARP2, gGlobalSoundSource);
            solo_menu_close();
        }
    } else {
        solo_reset_repeat();
    }
}

static const struct SoloOption sMainOptions[] = {
    { "CONTROLS",    SOLO_OPT_SUBMENU, .submenu = &sMenuControls },
    { "CAMERA",      SOLO_OPT_SUBMENU, .submenu = &sMenuCamera },
    { "DISPLAY",     SOLO_OPT_SUBMENU, .submenu = &sMenuDisplay },
    { "SOUND",       SOLO_OPT_SUBMENU, .submenu = &sMenuSound },
    { "PLAYER",      SOLO_OPT_SUBMENU, .submenu = &sMenuPlayer },
    { "EXIT GAME",   SOLO_OPT_ACTION,  .action = solo_exit_game },
};

static const struct SoloOption sPlayerOptions[] = {
    { "Character",        SOLO_OPT_CHOICE, .uval = &configPlayerModel, .apply = solo_player_apply, .change = solo_change_character, .valueText = solo_character_value },
    { "Color Palette",    SOLO_OPT_CHOICE, .change = solo_change_palette, .valueText = solo_palette_value },
    { "Player Options",   SOLO_OPT_ACTION, .action = solo_open_player_panel },
};

static const struct SoloOption sCameraOptions[] = {
    { "Invert X",         SOLO_OPT_BOOL,   .bval = &configCameraInvertX, .apply = solo_camera_apply },
    { "Invert Y",         SOLO_OPT_BOOL,   .bval = &configCameraInvertY, .apply = solo_camera_apply },
    { "Free Camera",      SOLO_OPT_BOOL,   .bval = &configEnableFreeCamera, .apply = solo_camera_apply },
    { "Free Camera Options",    SOLO_OPT_SUBMENU, .submenu = &sMenuFreeCamera },
    { "Romhack Camera",   SOLO_OPT_CHOICE, .uval = &configEnableRomhackCamera, .choices = sRomhackCameraChoices, .choiceCount = ARRAY_COUNT(sRomhackCameraChoices), .apply = solo_camera_apply },
    { "Romhack Camera Options", SOLO_OPT_SUBMENU, .submenu = &sMenuRomhackCamera },
};

static const struct SoloOption sFreeCameraOptions[] = {
    { "Analog Camera",    SOLO_OPT_BOOL,  .bval = &configFreeCameraAnalog, .apply = solo_camera_apply },
    { "L Centering",      SOLO_OPT_BOOL,  .bval = &configFreeCameraLCentering, .apply = solo_camera_apply },
    { "Use D-Pad",        SOLO_OPT_BOOL,  .bval = &configFreeCameraDPadBehavior, .apply = solo_camera_apply },
    { "Collision",        SOLO_OPT_BOOL,  .bval = &configFreeCameraHasCollision, .apply = solo_camera_apply },
    { "Mouse Look",       SOLO_OPT_BOOL,  .bval = &configFreeCameraMouse, .apply = solo_camera_apply },
    { "X Sensitivity",    SOLO_OPT_RANGE, .uval = &configFreeCameraXSens, .min = 1, .max = 100, .step = 5, .apply = solo_camera_apply },
    { "Y Sensitivity",    SOLO_OPT_RANGE, .uval = &configFreeCameraYSens, .min = 1, .max = 100, .step = 5, .apply = solo_camera_apply },
    { "Aggression",       SOLO_OPT_RANGE, .uval = &configFreeCameraAggr, .min = 0, .max = 100, .step = 5, .apply = solo_camera_apply },
    { "Pan Level",        SOLO_OPT_RANGE, .uval = &configFreeCameraPan, .min = 0, .max = 100, .step = 5, .apply = solo_camera_apply },
    { "Deceleration",     SOLO_OPT_RANGE, .uval = &configFreeCameraDegrade, .min = 0, .max = 100, .step = 5, .apply = solo_camera_apply },
};

static const struct SoloOption sRomhackCameraOptions[] = {
    { "Bowser Fights",    SOLO_OPT_BOOL,   .bval = &configRomhackCameraBowserFights, .apply = solo_camera_apply },
    { "Collision",        SOLO_OPT_BOOL,   .bval = &configRomhackCameraHasCollision, .apply = solo_camera_apply },
    { "L Centering",      SOLO_OPT_BOOL,   .bval = &configRomhackCameraHasCentering, .apply = solo_camera_apply },
    { "Use D-Pad",        SOLO_OPT_BOOL,   .bval = &configRomhackCameraDPadBehavior, .apply = solo_camera_apply },
    { "Slow Fall",        SOLO_OPT_BOOL,   .bval = &configRomhackCameraSlowFall, .apply = solo_camera_apply },
    { "Toxic Gas",        SOLO_OPT_BOOL,   .bval = &configCameraToxicGas, .apply = solo_camera_apply },
};

static const struct SoloOption sControlsOptions[] = {
    { "A Button",        SOLO_OPT_BIND,   .uval = configKeyA },
    { "B Button",        SOLO_OPT_BIND,   .uval = configKeyB },
    { "Start Button",    SOLO_OPT_BIND,   .uval = configKeyStart },
    { "L Trigger",       SOLO_OPT_BIND,   .uval = configKeyL },
    { "R Trigger",       SOLO_OPT_BIND,   .uval = configKeyR },
    { "Z Trigger",       SOLO_OPT_BIND,   .uval = configKeyZ },
    { "C-Up",            SOLO_OPT_BIND,   .uval = configKeyCUp },
    { "C-Down",          SOLO_OPT_BIND,   .uval = configKeyCDown },
    { "C-Left",          SOLO_OPT_BIND,   .uval = configKeyCLeft },
    { "C-Right",         SOLO_OPT_BIND,   .uval = configKeyCRight },
    { "Stick Up",        SOLO_OPT_BIND,   .uval = configKeyStickUp },
    { "Stick Down",      SOLO_OPT_BIND,   .uval = configKeyStickDown },
    { "Stick Left",      SOLO_OPT_BIND,   .uval = configKeyStickLeft },
    { "Stick Right",     SOLO_OPT_BIND,   .uval = configKeyStickRight },
    { "Stick Deadzone",  SOLO_OPT_RANGE,  .uval = &configStickDeadzone, .min = 0, .max = 100, .step = 5 },
    { "Rumble",          SOLO_OPT_RANGE,  .uval = &configRumbleStrength, .min = 0, .max = 100, .step = 5 },
    { "Restore Defaults", SOLO_OPT_ACTION, .action = solo_restore_default_binds },
    { "Control Options",  SOLO_OPT_ACTION, .action = solo_open_controls_panel },
};

static const struct SoloOption sDisplayOptions[] = {
    { "Fullscreen",       SOLO_OPT_BOOL,   .bval = &configWindow.fullscreen, .apply = solo_display_apply },
    { "Force 4:3",        SOLO_OPT_BOOL,   .bval = &configForce4By3, .apply = solo_display_apply },
    { "Show FPS",         SOLO_OPT_BOOL,   .bval = &configShowFPS },
    { "Vsync",            SOLO_OPT_BOOL,   .bval = &configWindow.vsync, .apply = solo_display_apply },
    { "Framerate",        SOLO_OPT_CHOICE, .uval = (u32 *)&configFramerateMode, .choices = sFramerateChoices, .choiceCount = ARRAY_COUNT(sFramerateChoices) },
    { "Frame Limit",      SOLO_OPT_CHOICE, .uval = &configFrameLimit, .choices = sFrameLimitChoices, .choiceCount = ARRAY_COUNT(sFrameLimitChoices), .enabled = solo_frame_limit_enabled },
    { "Interpolation",    SOLO_OPT_CHOICE, .uval = &configInterpolationMode, .choices = sInterpolationChoices, .choiceCount = ARRAY_COUNT(sInterpolationChoices) },
    { "Filtering",        SOLO_OPT_CHOICE, .uval = &configFiltering, .choices = sFilteringChoices, .choiceCount = ARRAY_COUNT(sFilteringChoices) },
    { "Antialiasing",     SOLO_OPT_CHOICE, .change = solo_change_msaa, .valueText = solo_msaa_value, .apply = solo_display_apply },
    { "Draw Distance",    SOLO_OPT_CHOICE, .uval = &configDrawDistance, .choices = sDrawDistanceChoices, .choiceCount = ARRAY_COUNT(sDrawDistanceChoices) },
};

static const struct SoloOption sSoundOptions[] = {
    { "Master",           SOLO_OPT_RANGE,  .uval = &configMasterVolume, .min = 0, .max = 127, .step = 8, .apply = solo_sound_apply },
    { "Music",            SOLO_OPT_RANGE,  .uval = &configMusicVolume, .min = 0, .max = 127, .step = 8, .apply = solo_sound_apply },
    { "Sfx",              SOLO_OPT_RANGE,  .uval = &configSfxVolume, .min = 0, .max = 127, .step = 8, .apply = solo_sound_apply },
    { "Environment",      SOLO_OPT_RANGE,  .uval = &configEnvVolume, .min = 0, .max = 127, .step = 8, .apply = solo_sound_apply },
    { "Fadeout",          SOLO_OPT_BOOL,   .bval = &configFadeoutDistantSounds },
    { "Mute Focus Loss",  SOLO_OPT_BOOL,   .bval = &configMuteFocusLoss },
    { "Sound Options",    SOLO_OPT_ACTION, .action = solo_open_sound_panel },
};

static struct SoloMenu sMenuMain = { "OPTIONS", sMainOptions, ARRAY_COUNT(sMainOptions), 0, 0, NULL };
static struct SoloMenu sMenuPlayer = { "PLAYER", sPlayerOptions, ARRAY_COUNT(sPlayerOptions), 0, 0, &sMenuMain };
static struct SoloMenu sMenuCamera = { "CAMERA", sCameraOptions, ARRAY_COUNT(sCameraOptions), 0, 0, &sMenuMain };
static struct SoloMenu sMenuFreeCamera = { "FREE CAMERA", sFreeCameraOptions, ARRAY_COUNT(sFreeCameraOptions), 0, 0, &sMenuCamera };
static struct SoloMenu sMenuRomhackCamera = { "ROMHACK CAMERA", sRomhackCameraOptions, ARRAY_COUNT(sRomhackCameraOptions), 0, 0, &sMenuCamera };
static struct SoloMenu sMenuControls = { "CONTROLS", sControlsOptions, ARRAY_COUNT(sControlsOptions), 0, 0, &sMenuMain };
static struct SoloMenu sMenuDisplay = { "DISPLAY", sDisplayOptions, ARRAY_COUNT(sDisplayOptions), 0, 0, &sMenuMain };
static struct SoloMenu sMenuSound = { "SOUND", sSoundOptions, ARRAY_COUNT(sSoundOptions), 0, 0, &sMenuMain };

void optmenu_update_and_render(void) {
    if (!sSoloOptionsOpen) {
        if (gPlayer1Controller->buttonPressed & R_TRIG) {
            solo_menu_open();
        }
        solo_draw_prompt();
        return;
    }

    solo_update_open_menu();
    if (sSoloOptionsOpen) {
        solo_draw_menu();
    }
}

#endif

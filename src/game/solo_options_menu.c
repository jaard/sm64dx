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
#include "pc/djui/djui_panel_camera.h"
#include "pc/djui/djui_panel_controls.h"
#include "pc/djui/djui_panel_controls_n64.h"
#include "pc/djui/djui_panel_display.h"
#include "pc/djui/djui_panel_dynos.h"
#include "pc/djui/djui_panel_language.h"
#include "pc/djui/djui_panel_menu_options.h"
#include "pc/djui/djui_panel_misc.h"
#include "pc/djui/djui_panel_pause.h"
#include "pc/djui/djui_panel_player.h"
#include "pc/djui/djui_panel_sound.h"
#include "pc/gfx/gfx_window_manager_api.h"
#include "pc/lua/utils/smlua_audio_utils.h"
#include "pc/network/network_player.h"
#include "pc/pc_main.h"
#include "sm64.h"
#include "types.h"

#define SOLO_OPT_VISIBLE_COUNT 4
#define SOLO_OPT_BUF_SIZE 64

enum SoloOptionType {
    SOLO_OPT_SUBMENU,
    SOLO_OPT_BOOL,
    SOLO_OPT_CHOICE,
    SOLO_OPT_RANGE,
    SOLO_OPT_ACTION,
    SOLO_OPT_BACK,
};

struct SoloOption;
struct SoloMenu;

struct SoloChoice {
    const char *label;
    u32 value;
};

typedef void (*SoloActionFn)(void);
typedef void (*SoloPanelCreateFn)(struct DjuiBase *caller);
typedef void (*SoloApplyFn)(void);
typedef void (*SoloChangeFn)(const struct SoloOption *opt, s32 dir);
typedef const char *(*SoloValueFn)(const struct SoloOption *opt, char *buf, size_t size);

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

static struct SoloMenu sMenuMain;
static struct SoloMenu sMenuPlayer;
static struct SoloMenu sMenuCamera;
static struct SoloMenu sMenuControls;
static struct SoloMenu sMenuDisplay;
static struct SoloMenu sMenuSound;
static struct SoloMenu sMenuMisc;

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
    sInputTimer = 0;
    sHoldCount = 0;
}

static void solo_menu_open(void) {
    play_sound(SOUND_MENU_CHANGE_SELECT, gGlobalSoundSource);
    sSoloOptionsOpen = true;
    sCurrentMenu = &sMenuMain;
    sInputTimer = 0;
    sHoldCount = 0;
}

bool solo_options_menu_is_open(void) {
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

static void solo_player_apply(void) {
    if (configPlayerModel >= CT_MAX) { configPlayerModel = CT_MARIO; }
    if (gNetworkPlayers[0].overrideModelIndex == gNetworkPlayers[0].modelIndex) {
        gNetworkPlayers[0].overrideModelIndex = configPlayerModel;
    }
    gNetworkPlayers[0].modelIndex = configPlayerModel;
    network_player_update_model(0);
}

static void solo_player_apply_default_palette(void) {
    if (configPlayerModel >= CT_MAX) { configPlayerModel = CT_MARIO; }

    configPlayerPalette = sDefaultCharacterPalettes[configPlayerModel];
    gNetworkPlayers[0].palette = configPlayerPalette;
    gNetworkPlayers[0].overridePalette = configPlayerPalette;
}

static void solo_open_djui(SoloPanelCreateFn createPanel) {
    solo_menu_close();
    djui_panel_pause_create(NULL);
    createPanel(NULL);
}

static void solo_open_player_panel(void) { solo_open_djui(djui_panel_player_create); }
static void solo_open_camera_panel(void) { solo_open_djui(djui_panel_camera_create); }
static void solo_open_controls_panel(void) { solo_open_djui(djui_panel_controls_create); }
static void solo_open_n64_binds_panel(void) { solo_open_djui(djui_panel_controls_n64_create); }
static void solo_open_display_panel(void) { solo_open_djui(djui_panel_display_create); }
static void solo_open_sound_panel(void) { solo_open_djui(djui_panel_sound_create); }
static void solo_open_misc_panel(void) { solo_open_djui(djui_panel_misc_create); }
static void solo_open_language_panel(void) { solo_open_djui(djui_panel_language_create); }
static void solo_open_menu_options_panel(void) { solo_open_djui(djui_panel_main_menu_create); }
static void solo_open_dynos_panel(void) { solo_open_djui(djui_panel_dynos_create); }

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

static const char *solo_character_value(UNUSED const struct SoloOption *opt, UNUSED char *buf, UNUSED size_t size) {
    if (configPlayerModel >= CT_MAX) { configPlayerModel = CT_MARIO; }
    return gCharacters[configPlayerModel].name;
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

static void solo_change_option(const struct SoloOption *opt, s32 dir) {
    if (opt->change) {
        opt->change(opt, dir);
        return;
    }

    switch (opt->type) {
        case SOLO_OPT_BOOL:   solo_change_bool(opt, dir);   break;
        case SOLO_OPT_CHOICE: solo_change_choice(opt, dir); break;
        case SOLO_OPT_RANGE:  solo_change_range(opt, dir);  break;
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
    switch (opt->type) {
        case SOLO_OPT_SUBMENU:
            sCurrentMenu = opt->submenu;
            break;
        case SOLO_OPT_ACTION:
            opt->action();
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

static void solo_text(s16 x, s16 y, const char *ascii, bool selected) {
    u8 text[SOLO_OPT_BUF_SIZE] = { DIALOG_CHAR_TERMINATOR };
    convert_string_ascii_to_sm64(text, ascii, false);

    s16 textX = get_str_x_pos_from_center(x, text, 10.0f);
    gDPSetEnvColor(gDisplayListHead++, 0, 0, 0, 255);
    print_generic_string(textX + 1, y - 1, text);
    gDPSetEnvColor(gDisplayListHead++, selected ? 255 : 255, selected ? 32 : 255, selected ? 32 : 255, 255);
    print_generic_string(textX, y, text);
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

static void solo_draw_menu(void) {
    solo_draw_title();

    if (sCurrentMenu->optCount > SOLO_OPT_VISIBLE_COUNT) {
        s16 scrollpos = 54 * ((f32)sCurrentMenu->scroll / (sCurrentMenu->optCount - SOLO_OPT_VISIBLE_COUNT));
        solo_draw_box(272, 90, 280, 208, 0x80, 0x80, 0x80);
        solo_draw_box(272, 90 + scrollpos, 280, 154 + scrollpos, 0xFF, 0xFF, 0xFF);
    }

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, 80, SCREEN_WIDTH, SCREEN_HEIGHT);

    for (s8 i = 0; i < sCurrentMenu->optCount; i++) {
        s16 y = 140 - 32 * i + sCurrentMenu->scroll * 32;
        if (y > 140 || y <= 32) { continue; }

        const struct SoloOption *opt = &sCurrentMenu->opts[i];
        bool selected = sCurrentMenu->select == i;
        s16 labelY = (opt->type == SOLO_OPT_SUBMENU || opt->type == SOLO_OPT_ACTION || opt->type == SOLO_OPT_BACK) ? y - 6 : y;
        char valueBuf[SOLO_OPT_BUF_SIZE];
        const char *value = solo_option_value(opt, valueBuf, sizeof(valueBuf));

        solo_text(160, labelY, opt->label, selected);
        if (value != NULL) {
            solo_text(160, y - 13, value, selected);
        }
    }

    gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (sCurrentMenu->optCount > 0) {
        s16 arrowY = 132 - (32 * (sCurrentMenu->select - sCurrentMenu->scroll));
        solo_text(72, arrowY, "<", false);
        solo_text(232, arrowY, ">", false);
    }

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
    solo_draw_prompt();
}

static void solo_move_selection(s32 dir) {
    struct SoloMenu *menu = (struct SoloMenu *)sCurrentMenu;
    menu->select += dir;
    if (menu->select < 0) {
        menu->select = menu->optCount - 1;
    } else if (menu->select >= menu->optCount) {
        menu->select = 0;
    }

    if (menu->select < menu->scroll) {
        menu->scroll = menu->select;
    } else if (menu->select > menu->scroll + SOLO_OPT_VISIBLE_COUNT - 1) {
        menu->scroll = menu->select - (SOLO_OPT_VISIBLE_COUNT - 1);
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
    { "PLAYER",      SOLO_OPT_SUBMENU, .submenu = &sMenuPlayer },
    { "CAMERA",      SOLO_OPT_SUBMENU, .submenu = &sMenuCamera },
    { "CONTROLS",    SOLO_OPT_SUBMENU, .submenu = &sMenuControls },
    { "DISPLAY",     SOLO_OPT_SUBMENU, .submenu = &sMenuDisplay },
    { "SOUND",       SOLO_OPT_SUBMENU, .submenu = &sMenuSound },
    { "MISC",        SOLO_OPT_SUBMENU, .submenu = &sMenuMisc },
    { "DYNOS PACKS", SOLO_OPT_ACTION,  .action = solo_open_dynos_panel },
    { "RETURN",      SOLO_OPT_BACK, .uval = NULL },
};

static const struct SoloOption sPlayerOptions[] = {
    { "CHARACTER",        SOLO_OPT_CHOICE, .uval = &configPlayerModel, .apply = solo_player_apply, .change = solo_change_character, .valueText = solo_character_value },
    { "PLAYER OPTIONS",   SOLO_OPT_ACTION, .action = solo_open_player_panel },
    { "RETURN",           SOLO_OPT_BACK, .uval = NULL },
};

static const struct SoloOption sCameraOptions[] = {
    { "INVERT X",         SOLO_OPT_BOOL,   .bval = &configCameraInvertX, .apply = solo_camera_apply },
    { "INVERT Y",         SOLO_OPT_BOOL,   .bval = &configCameraInvertY, .apply = solo_camera_apply },
    { "FREE CAMERA",      SOLO_OPT_BOOL,   .bval = &configEnableFreeCamera, .apply = solo_camera_apply },
    { "ROMHACK CAMERA",   SOLO_OPT_CHOICE, .uval = &configEnableRomhackCamera, .choices = sRomhackCameraChoices, .choiceCount = ARRAY_COUNT(sRomhackCameraChoices), .apply = solo_camera_apply },
    { "CAMERA OPTIONS",   SOLO_OPT_ACTION, .action = solo_open_camera_panel },
    { "RETURN",           SOLO_OPT_BACK, .uval = NULL },
};

static const struct SoloOption sControlsOptions[] = {
    { "DEADZONE",         SOLO_OPT_RANGE,  .uval = &configStickDeadzone, .min = 0, .max = 100, .step = 5 },
    { "RUMBLE",           SOLO_OPT_RANGE,  .uval = &configRumbleStrength, .min = 0, .max = 100, .step = 5 },
    { "N64 BINDS",        SOLO_OPT_ACTION, .action = solo_open_n64_binds_panel },
    { "CONTROL OPTIONS",  SOLO_OPT_ACTION, .action = solo_open_controls_panel },
    { "RETURN",           SOLO_OPT_BACK, .uval = NULL },
};

static const struct SoloOption sDisplayOptions[] = {
    { "FULLSCREEN",       SOLO_OPT_BOOL,   .bval = &configWindow.fullscreen, .apply = solo_display_apply },
    { "FORCE 4:3",        SOLO_OPT_BOOL,   .bval = &configForce4By3, .apply = solo_display_apply },
    { "SHOW FPS",         SOLO_OPT_BOOL,   .bval = &configShowFPS },
    { "VSYNC",            SOLO_OPT_BOOL,   .bval = &configWindow.vsync, .apply = solo_display_apply },
    { "FRAMERATE",        SOLO_OPT_CHOICE, .uval = (u32 *)&configFramerateMode, .choices = sFramerateChoices, .choiceCount = ARRAY_COUNT(sFramerateChoices) },
    { "FRAME LIMIT",      SOLO_OPT_CHOICE, .uval = &configFrameLimit, .choices = sFrameLimitChoices, .choiceCount = ARRAY_COUNT(sFrameLimitChoices) },
    { "INTERPOLATION",    SOLO_OPT_CHOICE, .uval = &configInterpolationMode, .choices = sInterpolationChoices, .choiceCount = ARRAY_COUNT(sInterpolationChoices) },
    { "FILTERING",        SOLO_OPT_CHOICE, .uval = &configFiltering, .choices = sFilteringChoices, .choiceCount = ARRAY_COUNT(sFilteringChoices) },
    { "ANTIALIASING",     SOLO_OPT_CHOICE, .change = solo_change_msaa, .valueText = solo_msaa_value, .apply = solo_display_apply },
    { "DRAW DISTANCE",    SOLO_OPT_CHOICE, .uval = &configDrawDistance, .choices = sDrawDistanceChoices, .choiceCount = ARRAY_COUNT(sDrawDistanceChoices) },
    { "DISPLAY OPTIONS",  SOLO_OPT_ACTION, .action = solo_open_display_panel },
    { "RETURN",           SOLO_OPT_BACK, .uval = NULL },
};

static const struct SoloOption sSoundOptions[] = {
    { "MASTER",           SOLO_OPT_RANGE,  .uval = &configMasterVolume, .min = 0, .max = 127, .step = 8, .apply = solo_sound_apply },
    { "MUSIC",            SOLO_OPT_RANGE,  .uval = &configMusicVolume, .min = 0, .max = 127, .step = 8, .apply = solo_sound_apply },
    { "SFX",              SOLO_OPT_RANGE,  .uval = &configSfxVolume, .min = 0, .max = 127, .step = 8, .apply = solo_sound_apply },
    { "ENVIRONMENT",      SOLO_OPT_RANGE,  .uval = &configEnvVolume, .min = 0, .max = 127, .step = 8, .apply = solo_sound_apply },
    { "FADEOUT",          SOLO_OPT_BOOL,   .bval = &configFadeoutDistantSounds },
    { "MUTE FOCUS LOSS",  SOLO_OPT_BOOL,   .bval = &configMuteFocusLoss },
    { "SOUND OPTIONS",    SOLO_OPT_ACTION, .action = solo_open_sound_panel },
    { "RETURN",           SOLO_OPT_BACK, .uval = NULL },
};

static const struct SoloOption sMiscOptions[] = {
    { "DISABLE POPUPS",   SOLO_OPT_BOOL,   .bval = &configDisablePopups },
    { "LANGUAGE",         SOLO_OPT_ACTION, .action = solo_open_language_panel },
    { "MENU OPTIONS",     SOLO_OPT_ACTION, .action = solo_open_menu_options_panel },
    { "MISC OPTIONS",     SOLO_OPT_ACTION, .action = solo_open_misc_panel },
    { "RETURN",           SOLO_OPT_BACK, .uval = NULL },
};

static struct SoloMenu sMenuMain = { "OPTIONS", sMainOptions, ARRAY_COUNT(sMainOptions), 0, 0, NULL };
static struct SoloMenu sMenuPlayer = { "PLAYER", sPlayerOptions, ARRAY_COUNT(sPlayerOptions), 0, 0, &sMenuMain };
static struct SoloMenu sMenuCamera = { "CAMERA", sCameraOptions, ARRAY_COUNT(sCameraOptions), 0, 0, &sMenuMain };
static struct SoloMenu sMenuControls = { "CONTROLS", sControlsOptions, ARRAY_COUNT(sControlsOptions), 0, 0, &sMenuMain };
static struct SoloMenu sMenuDisplay = { "DISPLAY", sDisplayOptions, ARRAY_COUNT(sDisplayOptions), 0, 0, &sMenuMain };
static struct SoloMenu sMenuSound = { "SOUND", sSoundOptions, ARRAY_COUNT(sSoundOptions), 0, 0, &sMenuMain };
static struct SoloMenu sMenuMisc = { "MISC", sMiscOptions, ARRAY_COUNT(sMiscOptions), 0, 0, &sMenuMain };

void solo_options_menu_update_and_render(void) {
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

#include "../lvgl/lvgl.h"
#include "../lvgl/drivers/display/fbdev.h"
#include "../lvgl/drivers/indev/evdev.h"
#include "ui/ui.h"
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <linux/joystick.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libgen.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include "../common/img/nothing.h"
#include "../common/common.h"
#include "../common/options.h"
#include "../common/theme.h"
#include "../common/ui_common.h"
#include "../common/config.h"
#include "../common/device.h"

__thread uint64_t start_ms = 0;

char *mux_prog;
static int js_fd;
static int js_fd_sys;

int NAV_DPAD_HOR;
int NAV_ANLG_HOR;
int NAV_DPAD_VER;
int NAV_ANLG_VER;
int NAV_A;
int NAV_B;

int turbo_mode = 0;
int msgbox_active = 0;
int input_disable = 0;
int SD2_found = 0;
int nav_sound = 0;
int bar_header = 0;
int bar_footer = 0;
char *osd_message;

struct mux_config config;
struct mux_device device;
struct theme_config theme;

int nav_moved = 1;
char *current_wall = "";
int current_item_index = 0;
int ui_count = 0;

lv_obj_t *msgbox_element = NULL;

int progress_onscreen = -1;

int bios_total, bios_current;
int raconfig_total, raconfig_current;
int catalogue_total, catalogue_current;
int content_total, content_current;
int music_total, music_current;
int save_total, save_current;
int screenshot_total, screenshot_current;
int look_total, look_current;
int language_total, language_current;
int network_total, network_current;

#define UI_COUNT 10
lv_obj_t *ui_objects[UI_COUNT];

typedef struct {
    int *total;
    int *current;
} Storage;

Storage bios, raconfig, catalogue, content, music, save, screenshot, look, language, network;

lv_group_t *ui_group;
lv_group_t *ui_group_value;
lv_group_t *ui_group_glyph;
lv_group_t *ui_group_panel;

struct help_msg {
    lv_obj_t *element;
    char *message;
};

void show_help(lv_obj_t *element_focused) {
    struct help_msg help_messages[] = {
            {ui_lblBIOS,       "HELP.BIOS"},
            {ui_lblConfig,     "HELP.CONFIG"},
            {ui_lblCatalogue,  "HELP.CATALOGUE"},
            {ui_lblConman,     "HELP.CONTENT"},
            {ui_lblMusic,      "HELP.MUSIC"},
            {ui_lblSave,       "HELP.SAVE"},
            {ui_lblScreenshot, "HELP.SCREENSHOT"},
            {ui_lblTheme,      "HELP.THEME"},
            {ui_lblLanguage,   "HELP.LANGUAGE"},
            {ui_lblNetwork,    "HELP.NETWORK"},
    };

    char *message = TG("No Help Information Found");
    int num_messages = sizeof(help_messages) / sizeof(help_messages[0]);

    for (int i = 0; i < num_messages; i++) {
        if (element_focused == help_messages[i].element) {
            message = help_messages[i].message;
            break;
        }
    }

    if (strlen(message) <= 1) message = TG("No Help Information Found");

    show_help_msgbox(ui_pnlHelp, ui_lblHelpHeader, ui_lblHelpContent,
                     TS(lv_label_get_text(element_focused)), TS(message));
}

void init_pointers(Storage *storage, int *total, int *current) {
    storage->total = total;
    storage->current = current;
}

static void dropdown_event_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        char buf[MAX_BUFFER_SIZE];
        lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
    }
}

void elements_events_init() {
    lv_obj_t *dropdowns[] = {
            ui_droBIOS,
            ui_droConfig,
            ui_droCatalogue,
            ui_droConman,
            ui_droMusic,
            ui_droSave,
            ui_droScreenshot,
            ui_droTheme,
            ui_droLanguage,
            ui_droNetwork
    };

    for (unsigned int i = 0; i < sizeof(dropdowns) / sizeof(dropdowns[0]); i++) {
        lv_obj_add_event_cb(dropdowns[i], dropdown_event_handler, LV_EVENT_ALL, NULL);
    }

    init_pointers(&bios, &bios_total, &bios_current);
    init_pointers(&raconfig, &raconfig_total, &raconfig_current);
    init_pointers(&catalogue, &catalogue_total, &catalogue_current);
    init_pointers(&content, &content_total, &content_current);
    init_pointers(&music, &music_total, &music_current);
    init_pointers(&save, &save_total, &save_current);
    init_pointers(&screenshot, &screenshot_total, &screenshot_current);
    init_pointers(&look, &look_total, &look_current);
    init_pointers(&language, &language_total, &language_current);
    init_pointers(&network, &network_total, &network_current);
}

void init_dropdown_settings() {
    Storage settings[] = {
            {bios.total,       bios.current},
            {raconfig.total,   raconfig.current},
            {catalogue.total,  catalogue.current},
            {content.total,    content.current},
            {music.total,      music.current},
            {save.total,       save.current},
            {screenshot.total, screenshot.current},
            {look.total,       look.current},
            {language.total,   language.current},
            {network.total,    network.current},
    };

    lv_obj_t *dropdowns[] = {
            ui_droBIOS,
            ui_droConfig,
            ui_droCatalogue,
            ui_droConman,
            ui_droMusic,
            ui_droSave,
            ui_droScreenshot,
            ui_droTheme,
            ui_droLanguage,
            ui_droNetwork
    };

    for (unsigned int i = 0; i < sizeof(settings) / sizeof(settings[0]); i++) {
        *(settings[i].total) = lv_dropdown_get_option_cnt(dropdowns[i]);
        *(settings[i].current) = lv_dropdown_get_selected(dropdowns[i]);
    }
}

void restore_storage_options() {
    lv_dropdown_set_selected(ui_droBIOS, config.STORAGE.BIOS);
    lv_dropdown_set_selected(ui_droConfig, config.STORAGE.CONFIG);
    lv_dropdown_set_selected(ui_droCatalogue, config.STORAGE.CATALOGUE);
    lv_dropdown_set_selected(ui_droConman, config.STORAGE.CONTENT);
    lv_dropdown_set_selected(ui_droMusic, config.STORAGE.MUSIC);
    lv_dropdown_set_selected(ui_droSave, config.STORAGE.SAVE);
    lv_dropdown_set_selected(ui_droScreenshot, config.STORAGE.SCREENSHOT);
    lv_dropdown_set_selected(ui_droTheme, config.STORAGE.THEME);
    lv_dropdown_set_selected(ui_droLanguage, config.STORAGE.LANGUAGE);
    lv_dropdown_set_selected(ui_droNetwork, config.STORAGE.NETWORK);
}

void save_storage_options() {
    int idx_bios = lv_dropdown_get_selected(ui_droBIOS);
    int idx_config = lv_dropdown_get_selected(ui_droConfig);
    int idx_catalogue = lv_dropdown_get_selected(ui_droCatalogue);
    int idx_content = lv_dropdown_get_selected(ui_droConman);
    int idx_music = lv_dropdown_get_selected(ui_droMusic);
    int idx_save = lv_dropdown_get_selected(ui_droSave);
    int idx_screenshot = lv_dropdown_get_selected(ui_droScreenshot);
    int idx_theme = lv_dropdown_get_selected(ui_droTheme);
    int idx_language = lv_dropdown_get_selected(ui_droLanguage);
    int idx_network = lv_dropdown_get_selected(ui_droNetwork);

    write_text_to_file("/run/muos/global/storage/bios", "w", INT, idx_bios);
    write_text_to_file("/run/muos/global/storage/config", "w", INT, idx_config);
    write_text_to_file("/run/muos/global/storage/catalogue", "w", INT, idx_catalogue);
    write_text_to_file("/run/muos/global/storage/content", "w", INT, idx_content);
    write_text_to_file("/run/muos/global/storage/music", "w", INT, idx_music);
    write_text_to_file("/run/muos/global/storage/save", "w", INT, idx_save);
    write_text_to_file("/run/muos/global/storage/screenshot", "w", INT, idx_screenshot);
    write_text_to_file("/run/muos/global/storage/theme", "w", INT, idx_theme);
    write_text_to_file("/run/muos/global/storage/language", "w", INT, idx_language);
    write_text_to_file("/run/muos/global/storage/network", "w", INT, idx_network);
}

void init_navigation_groups() {
    lv_obj_t *ui_objects_panel[] = {
            ui_pnlBIOS,
            ui_pnlConfig,
            ui_pnlCatalogue,
            ui_pnlConman,
            ui_pnlMusic,
            ui_pnlSave,
            ui_pnlScreenshot,
            ui_pnlTheme,
            ui_pnlLanguage,
            ui_pnlNetwork,
    };

    ui_objects[0] = ui_lblBIOS;
    ui_objects[1] = ui_lblConfig;
    ui_objects[2] = ui_lblCatalogue;
    ui_objects[3] = ui_lblConman;
    ui_objects[4] = ui_lblMusic;
    ui_objects[5] = ui_lblSave;
    ui_objects[6] = ui_lblScreenshot;
    ui_objects[7] = ui_lblTheme;
    ui_objects[8] = ui_lblLanguage;
    ui_objects[9] = ui_lblNetwork;

    lv_obj_t *ui_objects_value[] = {
            ui_droBIOS,
            ui_droConfig,
            ui_droCatalogue,
            ui_droConman,
            ui_droMusic,
            ui_droSave,
            ui_droScreenshot,
            ui_droTheme,
            ui_droLanguage,
            ui_droNetwork
    };

    lv_obj_t *ui_objects_glyph[] = {
            ui_icoBIOS,
            ui_icoConfig,
            ui_icoCatalogue,
            ui_icoConman,
            ui_icoMusic,
            ui_icoSave,
            ui_icoScreenshot,
            ui_icoTheme,
            ui_icoLanguage,
            ui_icoNetwork
    };

    apply_theme_list_panel(&theme, &device, ui_pnlBIOS);
    apply_theme_list_panel(&theme, &device, ui_pnlConfig);
    apply_theme_list_panel(&theme, &device, ui_pnlCatalogue);
    apply_theme_list_panel(&theme, &device, ui_pnlConman);
    apply_theme_list_panel(&theme, &device, ui_pnlMusic);
    apply_theme_list_panel(&theme, &device, ui_pnlSave);
    apply_theme_list_panel(&theme, &device, ui_pnlScreenshot);
    apply_theme_list_panel(&theme, &device, ui_pnlTheme);
    apply_theme_list_panel(&theme, &device, ui_pnlLanguage);
    apply_theme_list_panel(&theme, &device, ui_pnlNetwork);

    apply_theme_list_item(&theme, ui_lblBIOS, TS("RetroArch BIOS"), false, true);
    apply_theme_list_item(&theme, ui_lblConfig, TS("RetroArch Configs"), false, true);
    apply_theme_list_item(&theme, ui_lblCatalogue, TS("Metadata Catalogue"), false, true);
    apply_theme_list_item(&theme, ui_lblConman, TS("Content Management"), false, true);
    apply_theme_list_item(&theme, ui_lblMusic, TS("Background Music"), false, true);
    apply_theme_list_item(&theme, ui_lblSave, TS("Save Games + Save States"), false, true);
    apply_theme_list_item(&theme, ui_lblScreenshot, TS("Screenshots"), false, true);
    apply_theme_list_item(&theme, ui_lblTheme, TS("Themes"), false, true);
    apply_theme_list_item(&theme, ui_lblLanguage, TS("Languages"), false, true);
    apply_theme_list_item(&theme, ui_lblNetwork, TS("Network Profiles"), false, true);

    apply_theme_list_glyph(&theme, ui_icoBIOS, mux_prog, "bios");
    apply_theme_list_glyph(&theme, ui_icoConfig, mux_prog, "config");
    apply_theme_list_glyph(&theme, ui_icoCatalogue, mux_prog, "catalogue");
    apply_theme_list_glyph(&theme, ui_icoConman, mux_prog, "content");
    apply_theme_list_glyph(&theme, ui_icoMusic, mux_prog, "music");
    apply_theme_list_glyph(&theme, ui_icoSave, mux_prog, "save");
    apply_theme_list_glyph(&theme, ui_icoScreenshot, mux_prog, "screenshot");
    apply_theme_list_glyph(&theme, ui_icoTheme, mux_prog, "theme");
    apply_theme_list_glyph(&theme, ui_icoLanguage, mux_prog, "language");
    apply_theme_list_glyph(&theme, ui_icoNetwork, mux_prog, "network");

    char options[MAX_BUFFER_SIZE];
    snprintf(options, sizeof(options), "%s\n%s\n%s", TS("SD1"), TS("SD2"), TS("AUTO"));
    apply_theme_list_drop_down(&theme, ui_droBIOS, options);
    apply_theme_list_drop_down(&theme, ui_droConfig, options);
    apply_theme_list_drop_down(&theme, ui_droCatalogue, options);
    apply_theme_list_drop_down(&theme, ui_droConman, options);
    apply_theme_list_drop_down(&theme, ui_droMusic, options);
    apply_theme_list_drop_down(&theme, ui_droSave, options);
    apply_theme_list_drop_down(&theme, ui_droScreenshot, options);
    apply_theme_list_drop_down(&theme, ui_droTheme, options);
    apply_theme_list_drop_down(&theme, ui_droLanguage, options);
    apply_theme_list_drop_down(&theme, ui_droNetwork, options);

    ui_group = lv_group_create();
    ui_group_value = lv_group_create();
    ui_group_glyph = lv_group_create();
    ui_group_panel = lv_group_create();

    ui_count = sizeof(ui_objects) / sizeof(ui_objects[0]);
    for (unsigned int i = 0; i < ui_count; i++) {
        lv_group_add_obj(ui_group, ui_objects[i]);
        lv_group_add_obj(ui_group_value, ui_objects_value[i]);
        lv_group_add_obj(ui_group_glyph, ui_objects_glyph[i]);
        lv_group_add_obj(ui_group_panel, ui_objects_panel[i]);
    }
}

void list_nav_prev(int steps) {
    play_sound("navigate", nav_sound, 0);
    for (int step = 0; step < steps; ++step) {
        if (current_item_index > 0) {
            current_item_index--;
            nav_prev(ui_group, 1);
            nav_prev(ui_group_value, 1);
            nav_prev(ui_group_glyph, 1);
            nav_prev(ui_group_panel, 1);
        }
    }
    update_scroll_position(theme.MUX.ITEM.COUNT, theme.MUX.ITEM.PANEL, ui_count, current_item_index, ui_pnlContent);
    nav_moved = 1;
}

void list_nav_next(int steps) {
    play_sound("navigate", nav_sound, 0);
    for (int step = 0; step < steps; ++step) {
        if (current_item_index < (ui_count - 1)) {
            current_item_index++;
            nav_next(ui_group, 1);
            nav_next(ui_group_value, 1);
            nav_next(ui_group_glyph, 1);
            nav_next(ui_group_panel, 1);
        }
    }
    update_scroll_position(theme.MUX.ITEM.COUNT, theme.MUX.ITEM.PANEL, ui_count, current_item_index, ui_pnlContent);
    nav_moved = 1;
}

void joystick_task() {
    struct input_event ev;
    int epoll_fd;
    struct epoll_event event, events[device.DEVICE.EVENT];

    int JOYUP_pressed = 0;
    int JOYDOWN_pressed = 0;
    int JOYHOTKEY_pressed = 0;
    int JOYHOTKEY_screenshot = 0;

    uint32_t nav_hold = 0; // Delay (millis) before scrolling again when up/down is held.
    uint32_t nav_tick = 0; // Clock tick (millis) when the navigation list was last scrolled.

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("Error creating EPOLL instance");
        return;
    }

    event.events = EPOLLIN;
    event.data.fd = js_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, js_fd, &event) == -1) {
        perror("Error with EPOLL controller");
        return;
    }

    event.data.fd = js_fd_sys;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, js_fd_sys, &event) == -1) {
        perror("Error with EPOLL controller");
        return;
    }

    while (1) {
        int num_events = epoll_wait(epoll_fd, events, device.DEVICE.EVENT, config.SETTINGS.ADVANCED.ACCELERATE);
        if (num_events == -1) {
            perror("Error with EPOLL wait event timer");
            continue;
        }

        for (int i = 0; i < num_events; i++) {
            if (events[i].data.fd == js_fd_sys) {
                ssize_t ret = read(js_fd_sys, &ev, sizeof(struct input_event));
                if (ret == -1) {
                    perror("Error reading input");
                    continue;
                }
                if (JOYHOTKEY_pressed == 1 && ev.type == EV_KEY && ev.value == 1 &&
                    (ev.code == device.RAW_INPUT.BUTTON.POWER_SHORT || ev.code == device.RAW_INPUT.BUTTON.POWER_LONG)) {
                    JOYHOTKEY_screenshot = 1;
                }
            } else if (events[i].data.fd == js_fd) {
                ssize_t ret = read(js_fd, &ev, sizeof(struct input_event));
                if (ret == -1) {
                    perror("Error reading input");
                    continue;
                }

                struct _lv_obj_t *element_focused = lv_group_get_focused(ui_group);
                switch (ev.type) {
                    case EV_KEY:
                        if (ev.value == 1) {
                            if (msgbox_active) {
                                if (ev.code == NAV_B) {
                                    play_sound("confirm", nav_sound, 1);
                                    msgbox_active = 0;
                                    progress_onscreen = 0;
                                    lv_obj_add_flag(msgbox_element, LV_OBJ_FLAG_HIDDEN);
                                }
                            } else {
                                if (ev.code == device.RAW_INPUT.BUTTON.MENU_LONG) {
                                    JOYHOTKEY_pressed = 1;
                                    JOYHOTKEY_screenshot = 0;
                                } else if (ev.code == NAV_A || ev.code == device.RAW_INPUT.ANALOG.LEFT.CLICK) {
                                    play_sound("confirm", nav_sound, 1);
                                    if (element_focused == ui_lblBIOS) {
                                        increase_option_value(ui_droBIOS,
                                                              &bios_current,
                                                              bios_total);
                                    } else if (element_focused == ui_lblConfig) {
                                        increase_option_value(ui_droConfig,
                                                              &raconfig_current,
                                                              raconfig_total);
                                    } else if (element_focused == ui_lblCatalogue) {
                                        increase_option_value(ui_droCatalogue,
                                                              &catalogue_current,
                                                              catalogue_total);
                                    } else if (element_focused == ui_lblConman) {
                                        increase_option_value(ui_droConman,
                                                              &content_current,
                                                              content_total);
                                    } else if (element_focused == ui_lblMusic) {
                                        increase_option_value(ui_droMusic,
                                                              &music_current,
                                                              music_total);
                                    } else if (element_focused == ui_lblSave) {
                                        increase_option_value(ui_droSave,
                                                              &save_current,
                                                              save_total);
                                    } else if (element_focused == ui_lblScreenshot) {
                                        increase_option_value(ui_droScreenshot,
                                                              &screenshot_current,
                                                              screenshot_total);
                                    } else if (element_focused == ui_lblTheme) {
                                        increase_option_value(ui_droTheme,
                                                              &look_current,
                                                              look_total);
                                    } else if (element_focused == ui_lblLanguage) {
                                        increase_option_value(ui_droLanguage,
                                                              &language_current,
                                                              language_total);
                                    } else if (element_focused == ui_lblNetwork) {
                                        increase_option_value(ui_droNetwork,
                                                              &network_current,
                                                              network_total);
                                    }
                                } else if (ev.code == NAV_B) {
                                    play_sound("back", nav_sound, 1);
                                    input_disable = 1;

                                    osd_message = TG("Saving Changes");
                                    lv_label_set_text(ui_lblMessage, osd_message);
                                    lv_obj_clear_flag(ui_pnlMessage, LV_OBJ_FLAG_HIDDEN);

                                    save_storage_options();

                                    write_text_to_file(MUOS_PDI_LOAD, "w", CHAR, "storage");
                                    return;
                                }
                            }
                        } else {
                            if ((ev.code == device.RAW_INPUT.BUTTON.MENU_SHORT ||
                                 ev.code == device.RAW_INPUT.BUTTON.MENU_LONG) && !JOYHOTKEY_screenshot) {
                                JOYHOTKEY_pressed = 0;
                                if (progress_onscreen == -1) {
                                    play_sound("confirm", nav_sound, 1);
                                    show_help(element_focused);
                                }
                            }
                        }
                        break;
                    case EV_ABS:
                        if (msgbox_active) {
                            break;
                        }
                        if (ev.code == NAV_DPAD_VER || ev.code == NAV_ANLG_VER) {
                            if (ev.value == -device.INPUT.AXIS || ev.value == -1) {
                                if (current_item_index == 0) {
                                    current_item_index = ui_count - 1;
                                    nav_prev(ui_group, 1);
                                    nav_prev(ui_group_value, 1);
                                    nav_prev(ui_group_glyph, 1);
                                    nav_prev(ui_group_panel, 1);
                                    update_scroll_position(theme.MUX.ITEM.COUNT, theme.MUX.ITEM.PANEL,
                                                           ui_count, current_item_index, ui_pnlContent);
                                    nav_moved = 1;
                                } else if (current_item_index > 0) {
                                    list_nav_prev(1);
                                }
                                JOYUP_pressed = 1;
                                nav_hold = 2 * config.SETTINGS.ADVANCED.ACCELERATE;
                                nav_tick = mux_tick();
                            } else if (ev.value == device.INPUT.AXIS || ev.value == 1) {
                                if (current_item_index == ui_count - 1) {
                                    current_item_index = 0;
                                    nav_next(ui_group, 1);
                                    nav_next(ui_group_value, 1);
                                    nav_next(ui_group_glyph, 1);
                                    nav_next(ui_group_panel, 1);
                                    update_scroll_position(theme.MUX.ITEM.COUNT, theme.MUX.ITEM.PANEL,
                                                           ui_count, current_item_index, ui_pnlContent);
                                    nav_moved = 1;
                                } else if (current_item_index < ui_count - 1) {
                                    list_nav_next(1);
                                }
                                JOYDOWN_pressed = 1;
                                nav_hold = 2 * config.SETTINGS.ADVANCED.ACCELERATE;
                                nav_tick = mux_tick();
                            } else {
                                JOYUP_pressed = 0;
                                JOYDOWN_pressed = 0;
                            }
                        } else if (ev.code == NAV_DPAD_HOR || ev.code == NAV_ANLG_HOR) {
                            if (ev.value == -device.INPUT.AXIS || ev.value == -1) {
                                play_sound("navigate", nav_sound, 0);
                                if (element_focused == ui_lblBIOS) {
                                    decrease_option_value(ui_droBIOS,
                                                          &bios_current,
                                                          bios_total);
                                } else if (element_focused == ui_lblConfig) {
                                    decrease_option_value(ui_droConfig,
                                                          &raconfig_current,
                                                          raconfig_total);
                                } else if (element_focused == ui_lblCatalogue) {
                                    decrease_option_value(ui_droCatalogue,
                                                          &catalogue_current,
                                                          catalogue_total);
                                } else if (element_focused == ui_lblConman) {
                                    decrease_option_value(ui_droConman,
                                                          &content_current,
                                                          content_total);
                                } else if (element_focused == ui_lblMusic) {
                                    decrease_option_value(ui_droMusic,
                                                          &music_current,
                                                          music_total);
                                } else if (element_focused == ui_lblSave) {
                                    decrease_option_value(ui_droSave,
                                                          &save_current,
                                                          save_total);
                                } else if (element_focused == ui_lblScreenshot) {
                                    decrease_option_value(ui_droScreenshot,
                                                          &screenshot_current,
                                                          screenshot_total);
                                } else if (element_focused == ui_lblTheme) {
                                    decrease_option_value(ui_droTheme,
                                                          &look_current,
                                                          look_total);
                                } else if (element_focused == ui_lblLanguage) {
                                    decrease_option_value(ui_droLanguage,
                                                          &language_current,
                                                          language_total);
                                } else if (element_focused == ui_lblNetwork) {
                                    decrease_option_value(ui_droNetwork,
                                                          &network_current,
                                                          network_total);
                                }
                            } else if (ev.value == device.INPUT.AXIS || ev.value == 1) {
                                play_sound("navigate", nav_sound, 0);
                                if (element_focused == ui_lblBIOS) {
                                    increase_option_value(ui_droBIOS,
                                                          &bios_current,
                                                          bios_total);
                                } else if (element_focused == ui_lblConfig) {
                                    increase_option_value(ui_droConfig,
                                                          &raconfig_current,
                                                          raconfig_total);
                                } else if (element_focused == ui_lblCatalogue) {
                                    increase_option_value(ui_droCatalogue,
                                                          &catalogue_current,
                                                          catalogue_total);
                                } else if (element_focused == ui_lblConman) {
                                    increase_option_value(ui_droConman,
                                                          &content_current,
                                                          content_total);
                                } else if (element_focused == ui_lblMusic) {
                                    increase_option_value(ui_droMusic,
                                                          &music_current,
                                                          music_total);
                                } else if (element_focused == ui_lblSave) {
                                    increase_option_value(ui_droSave,
                                                          &save_current,
                                                          save_total);
                                } else if (element_focused == ui_lblScreenshot) {
                                    increase_option_value(ui_droScreenshot,
                                                          &screenshot_current,
                                                          screenshot_total);
                                } else if (element_focused == ui_lblTheme) {
                                    increase_option_value(ui_droTheme,
                                                          &look_current,
                                                          look_total);
                                } else if (element_focused == ui_lblLanguage) {
                                    increase_option_value(ui_droLanguage,
                                                          &language_current,
                                                          language_total);
                                } else if (element_focused == ui_lblNetwork) {
                                    increase_option_value(ui_droNetwork,
                                                          &network_current,
                                                          network_total);
                                }
                            }
                        }
                        break;
                    default:
                        break;
                }
            }
            refresh_screen();
        }

        // Handle menu acceleration.
        if (mux_tick() - nav_tick >= nav_hold) {
            if (JOYUP_pressed && current_item_index > 0) {
                list_nav_prev(1);
                nav_hold = config.SETTINGS.ADVANCED.ACCELERATE;
                nav_tick = mux_tick();
            } else if (JOYDOWN_pressed && current_item_index < ui_count - 1) {
                list_nav_next(1);
                nav_hold = config.SETTINGS.ADVANCED.ACCELERATE;
                nav_tick = mux_tick();
            }
        }

        if (!atoi(read_line_from_file("/tmp/hdmi_in_use", 1)) || config.SETTINGS.ADVANCED.HDMIOUTPUT) {
            if (ev.type == EV_KEY && ev.value == 1 &&
                (ev.code == device.RAW_INPUT.BUTTON.VOLUME_DOWN || ev.code == device.RAW_INPUT.BUTTON.VOLUME_UP)) {
                if (JOYHOTKEY_pressed) {
                    progress_onscreen = 1;
                    lv_obj_add_flag(ui_pnlProgressVolume, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(ui_pnlProgressBrightness, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(ui_icoProgressBrightness, "\uF185");
                    lv_bar_set_value(ui_barProgressBrightness, atoi(read_text_from_file(BRIGHT_PERC)), LV_ANIM_OFF);
                } else {
                    progress_onscreen = 2;
                    lv_obj_add_flag(ui_pnlProgressBrightness, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(ui_pnlProgressVolume, LV_OBJ_FLAG_HIDDEN);
                    int volume = atoi(read_text_from_file(VOLUME_PERC));
                    switch (volume) {
                        default:
                        case 0:
                            lv_label_set_text(ui_icoProgressVolume, "\uF6A9");
                            break;
                        case 1 ... 46:
                            lv_label_set_text(ui_icoProgressVolume, "\uF026");
                            break;
                        case 47 ... 71:
                            lv_label_set_text(ui_icoProgressVolume, "\uF027");
                            break;
                        case 72 ... 100:
                            lv_label_set_text(ui_icoProgressVolume, "\uF028");
                            break;
                    }
                    lv_bar_set_value(ui_barProgressVolume, volume, LV_ANIM_OFF);
                }
            }
        }

        if (file_exist("/tmp/hdmi_do_refresh")) {
            if (atoi(read_text_from_file("/tmp/hdmi_do_refresh"))) {
                remove("/tmp/hdmi_do_refresh");
                lv_obj_invalidate(ui_pnlHeader);
                lv_obj_invalidate(ui_pnlContent);
                lv_obj_invalidate(ui_pnlFooter);
            }
        }

        refresh_screen();
    }
}

void init_elements() {
    lv_obj_move_foreground(ui_pnlFooter);
    lv_obj_move_foreground(ui_pnlHeader);
    lv_obj_move_foreground(ui_pnlHelp);
    lv_obj_move_foreground(ui_pnlProgressBrightness);
    lv_obj_move_foreground(ui_pnlProgressVolume);

    if (bar_footer) {
        lv_obj_set_style_bg_opa(ui_pnlFooter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (bar_header) {
        lv_obj_set_style_bg_opa(ui_pnlHeader, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_label_set_text(ui_lblPreviewHeader, "");
    lv_label_set_text(ui_lblPreviewHeaderGlyph, "");

    process_visual_element(CLOCK, ui_lblDatetime);
    process_visual_element(BLUETOOTH, ui_staBluetooth);
    process_visual_element(NETWORK, ui_staNetwork);
    process_visual_element(BATTERY, ui_staCapacity);

    lv_label_set_text(ui_lblMessage, osd_message);

    lv_label_set_text(ui_lblNavB, TG("Save"));

    lv_obj_t *nav_hide[] = {
            ui_lblNavAGlyph,
            ui_lblNavA,
            ui_lblNavCGlyph,
            ui_lblNavC,
            ui_lblNavXGlyph,
            ui_lblNavX,
            ui_lblNavYGlyph,
            ui_lblNavY,
            ui_lblNavZGlyph,
            ui_lblNavZ,
            ui_lblNavMenuGlyph,
            ui_lblNavMenu
    };

    for (int i = 0; i < sizeof(nav_hide) / sizeof(nav_hide[0]); i++) {
        lv_obj_add_flag(nav_hide[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(nav_hide[i], LV_OBJ_FLAG_FLOATING);
    }

    lv_obj_set_user_data(ui_lblBIOS, "bios");
    lv_obj_set_user_data(ui_lblConfig, "config");
    lv_obj_set_user_data(ui_lblCatalogue, "catalogue");
    lv_obj_set_user_data(ui_lblConman, "content");
    lv_obj_set_user_data(ui_lblMusic, "music");
    lv_obj_set_user_data(ui_lblSave, "save");
    lv_obj_set_user_data(ui_lblScreenshot, "screenshot");
    lv_obj_set_user_data(ui_lblTheme, "theme");
    lv_obj_set_user_data(ui_lblLanguage, "language");
    lv_obj_set_user_data(ui_lblNetwork, "network");

    char *overlay = load_overlay_image();
    if (strlen(overlay) > 0 && theme.MISC.IMAGE_OVERLAY) {
        lv_obj_t *overlay_img = lv_img_create(ui_screen);
        lv_img_set_src(overlay_img, overlay);
        lv_obj_move_foreground(overlay_img);
    }

    if (TEST_IMAGE) display_testing_message(ui_screen);
}

void glyph_task() {
    // TODO: Bluetooth connectivity!
    //update_bluetooth_status(ui_staBluetooth, &theme);

    update_network_status(ui_staNetwork, &theme);
    update_battery_capacity(ui_staCapacity, &theme);

    if (progress_onscreen > 0) {
        progress_onscreen -= 1;
    } else {
        if (!lv_obj_has_flag(ui_pnlProgressBrightness, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(ui_pnlProgressBrightness, LV_OBJ_FLAG_HIDDEN);
        }
        if (!lv_obj_has_flag(ui_pnlProgressVolume, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(ui_pnlProgressVolume, LV_OBJ_FLAG_HIDDEN);
        }
        if (!msgbox_active) {
            progress_onscreen = -1;
        }
    }
}

void ui_refresh_task() {
    update_bars(ui_barProgressBrightness, ui_barProgressVolume);

    if (nav_moved) {
        if (lv_group_get_obj_count(ui_group) > 0) {
            static char old_wall[MAX_BUFFER_SIZE];
            static char new_wall[MAX_BUFFER_SIZE];

            snprintf(old_wall, sizeof(old_wall), "%s", current_wall);
            snprintf(new_wall, sizeof(new_wall), "%s", load_wallpaper(
                    ui_screen, ui_group, theme.MISC.ANIMATED_BACKGROUND, theme.MISC.RANDOM_BACKGROUND));

            if (strcasecmp(new_wall, old_wall) != 0) {
                strcpy(current_wall, new_wall);
                if (strlen(new_wall) > 3) {
                    if (theme.MISC.RANDOM_BACKGROUND) {
                        load_image_random(ui_imgWall, new_wall);
                    } else {
                        switch (theme.MISC.ANIMATED_BACKGROUND) {
                            case 1:
                                lv_gif_set_src(lv_gif_create(ui_pnlWall), new_wall);
                                break;
                            case 2:
                                load_image_animation(ui_imgWall, theme.ANIMATION.ANIMATION_DELAY, new_wall);
                                break;
                            default:
                                lv_img_set_src(ui_imgWall, new_wall);
                                break;
                        }
                    }
                } else {
                    lv_img_set_src(ui_imgWall, &ui_image_Nothing);
                }
            }

            static char static_image[MAX_BUFFER_SIZE];
            snprintf(static_image, sizeof(static_image), "%s",
                     load_static_image(ui_screen, ui_group));

            if (strlen(static_image) > 0) {
                printf("LOADING STATIC IMAGE: %s\n", static_image);

                switch (theme.MISC.STATIC_ALIGNMENT) {
                    case 0: // Bottom + Front
                        lv_obj_set_align(ui_imgBox, LV_ALIGN_BOTTOM_RIGHT);
                        lv_obj_move_foreground(ui_pnlBox);
                        break;
                    case 1: // Middle + Front
                        lv_obj_set_align(ui_imgBox, LV_ALIGN_RIGHT_MID);
                        lv_obj_move_foreground(ui_pnlBox);
                        break;
                    case 2: // Top + Front
                        lv_obj_set_align(ui_imgBox, LV_ALIGN_TOP_RIGHT);
                        lv_obj_move_foreground(ui_pnlBox);
                        break;
                    case 3: // Fullscreen + Behind
                        lv_obj_set_height(ui_pnlBox, device.MUX.HEIGHT);
                        lv_obj_set_align(ui_imgBox, LV_ALIGN_BOTTOM_RIGHT);
                        lv_obj_move_background(ui_pnlBox);
                        lv_obj_move_background(ui_pnlWall);
                        break;
                    case 4: // Fullscreen + Front
                        lv_obj_set_height(ui_pnlBox, device.MUX.HEIGHT);
                        lv_obj_set_align(ui_imgBox, LV_ALIGN_BOTTOM_RIGHT);
                        lv_obj_move_foreground(ui_pnlBox);
                        break;
                }

                lv_img_set_src(ui_imgBox, static_image);
            } else {
                lv_img_set_src(ui_imgBox, &ui_image_Nothing);
            }
        }
        lv_obj_invalidate(ui_pnlContent);
        nav_moved = 0;
    }
}

int main(int argc, char *argv[]) {
    (void) argc;

    mux_prog = basename(argv[0]);
    load_device(&device);


    lv_init();
    fbdev_init(device.SCREEN.DEVICE);

    static lv_disp_draw_buf_t disp_buf;
    uint32_t disp_buf_size = device.SCREEN.WIDTH * device.SCREEN.HEIGHT;

    lv_color_t * buf1 = (lv_color_t *) malloc(disp_buf_size * sizeof(lv_color_t));
    lv_color_t * buf2 = (lv_color_t *) malloc(disp_buf_size * sizeof(lv_color_t));

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, disp_buf_size);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = device.SCREEN.WIDTH;
    disp_drv.ver_res = device.SCREEN.HEIGHT;
    disp_drv.sw_rotate = device.SCREEN.ROTATE;
    disp_drv.rotated = device.SCREEN.ROTATE;
    disp_drv.full_refresh = 0;
    disp_drv.direct_mode = 0;
    lv_disp_drv_register(&disp_drv);

    load_config(&config);
    load_theme(&theme, &config, &device, basename(argv[0]));
    load_language(mux_prog);

    ui_common_screen_init(&theme, &device, TS("STORAGE PREFERENCE"));
    ui_init(ui_pnlContent);
    init_elements();

    lv_obj_set_user_data(ui_screen, basename(argv[0]));

    lv_label_set_text(ui_lblDatetime, get_datetime());
    switch (theme.MISC.NAVIGATION_TYPE) {
        case 1:
            NAV_DPAD_HOR = device.RAW_INPUT.DPAD.DOWN;
            NAV_ANLG_HOR = device.RAW_INPUT.ANALOG.LEFT.DOWN;
            NAV_DPAD_VER = device.RAW_INPUT.DPAD.RIGHT;
            NAV_ANLG_VER = device.RAW_INPUT.ANALOG.LEFT.RIGHT;
            break;
        default:
            NAV_DPAD_HOR = device.RAW_INPUT.DPAD.RIGHT;
            NAV_ANLG_HOR = device.RAW_INPUT.ANALOG.LEFT.RIGHT;
            NAV_DPAD_VER = device.RAW_INPUT.DPAD.DOWN;
            NAV_ANLG_VER = device.RAW_INPUT.ANALOG.LEFT.DOWN;
    }

    switch (config.SETTINGS.ADVANCED.SWAP) {
        case 1:
            NAV_A = device.RAW_INPUT.BUTTON.B;
            NAV_B = device.RAW_INPUT.BUTTON.A;
            break;
        default:
            NAV_A = device.RAW_INPUT.BUTTON.A;
            NAV_B = device.RAW_INPUT.BUTTON.B;
            break;
    }

    current_wall = load_wallpaper(ui_screen, NULL, theme.MISC.ANIMATED_BACKGROUND, theme.MISC.RANDOM_BACKGROUND);
    if (strlen(current_wall) > 3) {
        if (theme.MISC.RANDOM_BACKGROUND) {
            load_image_random(ui_imgWall, current_wall);
        } else {
            switch (theme.MISC.ANIMATED_BACKGROUND) {
                case 1:
                    lv_gif_set_src(lv_gif_create(ui_pnlWall), current_wall);
                    break;
                case 2:
                    load_image_animation(ui_imgWall, theme.ANIMATION.ANIMATION_DELAY, current_wall);
                    break;
                default:
                    lv_img_set_src(ui_imgWall, current_wall);
                    break;
            }
        }
    } else {
        lv_img_set_src(ui_imgWall, &ui_image_Nothing);
    }

    load_font_text(basename(argv[0]), ui_screen);
    load_font_section(basename(argv[0]), FONT_PANEL_FOLDER, ui_pnlContent);
    load_font_section(mux_prog, FONT_HEADER_FOLDER, ui_pnlHeader);
    load_font_section(mux_prog, FONT_FOOTER_FOLDER, ui_pnlFooter);

    if (config.SETTINGS.GENERAL.SOUND) {
        if (SDL_Init(SDL_INIT_AUDIO) >= 0) {
            Mix_Init(0);
            Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
            printf("SDL init success!\n");
            nav_sound = 1;
        } else {
            fprintf(stderr, "Failed to init SDL\n");
        }
    }

    init_navigation_groups();
    elements_events_init();

    restore_storage_options();
    init_dropdown_settings();

    struct dt_task_param dt_par;
    struct bat_task_param bat_par;
    struct osd_task_param osd_par;

    dt_par.lblDatetime = ui_lblDatetime;
    bat_par.staCapacity = ui_staCapacity;
    osd_par.lblMessage = ui_lblMessage;
    osd_par.pnlMessage = ui_pnlMessage;
    osd_par.count = 0;

    js_fd = open(device.INPUT.EV1, O_RDONLY);
    if (js_fd < 0) {
        perror("Failed to open joystick device");
        return 1;
    }

    js_fd_sys = open(device.INPUT.EV0, O_RDONLY);
    if (js_fd_sys < 0) {
        perror("Failed to open joystick device");
        return 1;
    }

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);

    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = evdev_read;
    indev_drv.user_data = (void *) (intptr_t) js_fd;

    lv_indev_drv_register(&indev_drv);

    lv_timer_t *datetime_timer = lv_timer_create(datetime_task, UINT16_MAX / 2, &dt_par);
    lv_timer_ready(datetime_timer);

    lv_timer_t *capacity_timer = lv_timer_create(capacity_task, UINT16_MAX / 2, &bat_par);
    lv_timer_ready(capacity_timer);

    lv_timer_t *osd_timer = lv_timer_create(osd_task, UINT16_MAX / 32, &osd_par);
    lv_timer_ready(osd_timer);

    lv_timer_t *glyph_timer = lv_timer_create(glyph_task, UINT16_MAX / 64, NULL);
    lv_timer_ready(glyph_timer);

    lv_timer_t *ui_refresh_timer = lv_timer_create(ui_refresh_task, UINT8_MAX / 4, NULL);
    lv_timer_ready(ui_refresh_timer);

    refresh_screen();
    joystick_task();

    close(js_fd);
    close(js_fd_sys);

    return 0;
}

uint32_t mux_tick(void) {
    struct timespec tv_now;
    clock_gettime(CLOCK_REALTIME, &tv_now);

    uint64_t now_ms = ((uint64_t) tv_now.tv_sec * 1000) + (tv_now.tv_nsec / 1000000);
    start_ms = start_ms || now_ms;

    return (uint32_t) (now_ms - start_ms);
}

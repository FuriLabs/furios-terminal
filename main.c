/**
 * Copyright 2021 Johannes Marbach
 * Copyright 2024 Bardia Moshiri
 * Copyright 2024 David Badiei
 *
 * This file is part of furios-terminal, hereafter referred to as the program.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */


#include "backends.h"
#include "command_line.h"
#include "config.h"
#include "indev.h"
#include "log.h"
#include "furios-terminal.h"
#include "terminal.h"
#include "theme.h"
#include "themes.h"
#include "lvm.h"
#include "termstr.h"

#include "lv_drv_conf.h"

#if USE_FBDEV
#include "lv_drivers/display/fbdev.h"
#endif /* USE_FBDEV */
#if USE_DRM
#include "lv_drivers/display/drm.h"
#endif /* USE_DRM */
#if USE_MINUI
#include "lv_drivers/display/minui.h"
#endif /* USE_MINUI */

#include "lvgl/lvgl.h"

#include "squeek2lvgl/sq2lv.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include <sys/reboot.h>
#include <sys/time.h>

/**
 * Static variables
 */

ul_cli_opts cli_opts;
ul_config_opts conf_opts;

bool is_alternate_theme = true;
bool is_password_obscured = true;
bool is_keyboard_hidden = true;

lv_obj_t *keyboard = NULL;
lv_obj_t* t_box = NULL;

/**
 * Static prototypes
 */

/**
 * Handle LV_EVENT_CLICKED events from the show/hide keyboard toggle button.
 *
 * @param event the event object
 */
static void toggle_kb_btn_clicked_cb(lv_event_t *event);

/**
 * Toggle between showing and hiding the keyboard.
 */
static void toggle_keyboard_hidden(void);

/**
 * Show / hide the keyboard
 *
 * @param is_hidden true if the keyboard should be hidden, false if it should be shown
 */
static void set_keyboard_hidden(bool is_hidden);

/**
 * Callback for the keyboard's vertical slide in / out animation.
 *
 * @param obj keyboard widget
 * @param value y position
 */
static void keyboard_anim_y_cb(void* obj, int32_t value);

/**
 * Handle LV_EVENT_VALUE_CHANGED events from the keyboard widget.
 *
 * @param event the event object
 */
static void keyboard_value_changed_cb(lv_event_t *event);

/**
 * Handle LV_EVENT_READY events from the keyboard widget.
 *
 * @param event the event object
 */
static void keyboard_ready_cb(lv_event_t *event);

static void send_sig_term();

/**
 * Handle termination signals sent to the process.
 *
 * @param signum the signal's number
 */
static void sigaction_handler(int signum);

static void update_tty_loop(lv_timer_t* timer);

static void update_tty(char * loc, int length, bool split);

static void clear_top_tty(char* loc);

static void split_and_add_tty();

static void clean_illegal_chars(char *loc);

/**
 * Static functions
 */

static void toggle_kb_btn_clicked_cb(lv_event_t *event) {
    LV_UNUSED(event);
    toggle_keyboard_hidden();
}

static void toggle_keyboard_hidden(void) {
    is_keyboard_hidden = !is_keyboard_hidden;
    set_keyboard_hidden(is_keyboard_hidden);
}

static void set_keyboard_hidden(bool is_hidden) {
    if (!conf_opts.general.animations) {
        lv_obj_set_y(keyboard, is_hidden ? lv_obj_get_height(keyboard) : 0);
        return;
    }

    lv_anim_t keyboard_anim;
    lv_anim_init(&keyboard_anim);
    lv_anim_set_var(&keyboard_anim, keyboard);
    lv_anim_set_values(&keyboard_anim, is_hidden ? 0 : lv_obj_get_height(keyboard), is_hidden ? lv_obj_get_y(keyboard) : 0);
    lv_anim_set_path_cb(&keyboard_anim, lv_anim_path_ease_out);
    lv_anim_set_time(&keyboard_anim, 500);
    lv_anim_set_exec_cb(&keyboard_anim, keyboard_anim_y_cb);
    lv_anim_start(&keyboard_anim);
}

static void keyboard_anim_y_cb(void* obj, int32_t value) {
    lv_obj_set_y(obj, value);
}

static void keyboard_value_changed_cb(lv_event_t *event) {
    lv_obj_t *kb = lv_event_get_target(event);

    uint16_t btn_id = lv_btnmatrix_get_selected_btn(kb);
    if (btn_id == LV_BTNMATRIX_BTN_NONE) {
        return;
    }

    if (sq2lv_is_layer_switcher(kb, btn_id)) {
        sq2lv_switch_layer(kb, btn_id);
        return;
    }

    lv_keyboard_def_event_cb(event);
}

static void keyboard_ready_cb(lv_event_t *event) {
    send_sig_term();
}

static void send_sig_term() {
    sigaction_handler(SIGTERM);
}

static void sigaction_handler(int signum) {
    LV_UNUSED(signum);
    exit(0);
}

static void update_tty_loop(lv_timer_t* timer) {
    update_tty(ul_terminal_update_interpret_buffer(),BUFFER_SIZE,true);
}

static void update_tty(char *loc,int length, bool split)
{
    if (term_needs_update && length > 0) {
        int maxlen = length == BUFFER_SIZE ? 9314 : 4096;
        if (strstr(loc, "\033[2J") != NULL)
            lv_textarea_set_text(t_box, "");
        if (split) {
            if ((strlen(lv_textarea_get_text(t_box)) + strlen(loc) >= 4096) && loc == ul_terminal_update_interpret_buffer()) {
                split_and_add_tty();
                return;
            }
        }
        if (strlen(lv_textarea_get_text(t_box)) >= maxlen)
            clear_top_tty(loc);
        
        remove_escape_codes(loc);
        
        clean_illegal_chars(loc);
        
        lv_textarea_add_text(t_box, loc);
        
        if (split)
            term_needs_update = false;
        for (char* i = loc; i < length + loc; i++)
            *i = '\0';
    }
}

static void split_and_add_tty()
{
    int j = ((strlen(lv_textarea_get_text(t_box)) + strlen(ul_terminal_update_interpret_buffer())) / 4096) + 1;
    int section_size = strlen(ul_terminal_update_interpret_buffer()) / j;

    for (int i = j; i >=1; i--) {
        char* text_section = (char*)malloc(section_size+1);
        memcpy(text_section, ul_terminal_update_interpret_buffer() + ((j - i) * section_size), section_size);
        text_section[section_size] = '\0';
        update_tty(text_section,section_size,false);
        if (text_section != NULL) {
            free(text_section);
            text_section = NULL;
        }
    }

    term_needs_update = false;
}

static void clear_top_tty(char* loc)
{
    char text_buffer[9314];

    strcpy(text_buffer,lv_textarea_get_text(t_box));

    char *new_text = text_buffer+strlen(loc);

    lv_textarea_set_text(t_box,new_text);
}

static void clean_illegal_chars(char *loc)
{
    char *src = loc, *dst = loc;

    while (*src != 0) {
        if (isalnum((unsigned char)*src) || ispunct((unsigned char)*src) || isspace((unsigned char)*src)) {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

/**
 * Main
 */

int main(int argc, char *argv[]) {
    /* Parse command line options */
    ul_cli_parse_opts(argc, argv, &cli_opts);

    /* Set up log level */
    if (cli_opts.verbose) {
        ul_log_set_level(UL_LOG_LEVEL_VERBOSE);
    }

    /* Announce ourselves */
    ul_log(UL_LOG_LEVEL_VERBOSE, "furios-terminal %s", UL_VERSION);

    /* Parse config files */
    ul_config_parse(cli_opts.config_files, cli_opts.num_config_files, &conf_opts);

    /* Initialise LVGL and set up logging callback */
    lv_init();

    /* Initialise display driver */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    /* Initialise framebuffer driver and query display size */
    uint32_t hor_res = 0;
    uint32_t ver_res = 0;
    uint32_t dpi = 0;

    switch (conf_opts.general.backend) {
#if USE_FBDEV
    case UL_BACKENDS_BACKEND_FBDEV:
        fbdev_init();
        fbdev_get_sizes(&hor_res, &ver_res, &dpi);
        disp_drv.flush_cb = fbdev_flush;
        break;
#endif /* USE_FBDEV */
#if USE_DRM
    case UL_BACKENDS_BACKEND_DRM:
        drm_init();
        drm_get_sizes((lv_coord_t *)&hor_res, (lv_coord_t *)&ver_res, &dpi);
        disp_drv.flush_cb = drm_flush;
        break;
#endif /* USE_DRM */
#if USE_MINUI
    case UL_BACKENDS_BACKEND_MINUI:
        minui_init();
        minui_get_sizes(&hor_res, &ver_res, &dpi);
        disp_drv.flush_cb = minui_flush;
        break;
#endif /* USE_MINUI */
    default:
        ul_log(UL_LOG_LEVEL_ERROR, "Unable to find suitable backend");
        exit(EXIT_FAILURE);
    }

    /* Override display parameters with command line options if necessary */
    if (cli_opts.hor_res > 0) {
        hor_res = cli_opts.hor_res;
    }
    if (cli_opts.ver_res > 0) {
        ver_res = cli_opts.ver_res;
    }
    if (cli_opts.dpi > 0) {
        dpi = cli_opts.dpi;
    }

    /* Prepare display buffer */
    const size_t buf_size = hor_res * ver_res / 10; /* At least 1/10 of the display size is recommended */
    lv_disp_draw_buf_t disp_buf;
    lv_color_t *buf = (lv_color_t *)malloc(buf_size * sizeof(lv_color_t));
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, buf_size);

    /* Register display driver */
    disp_drv.draw_buf = &disp_buf;
    disp_drv.hor_res = hor_res;
    disp_drv.ver_res = ver_res;
    disp_drv.offset_x = cli_opts.x_offset;
    disp_drv.offset_y = cli_opts.y_offset;
    disp_drv.dpi = dpi;
    lv_disp_drv_register(&disp_drv);

    /* Connect input devices */
    ul_indev_auto_connect(conf_opts.input.keyboard, conf_opts.input.pointer, conf_opts.input.touchscreen);
    ul_indev_set_up_mouse_cursor();

    /* Prevent scrolling when keyboard is off-screen */
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    /* Figure out a few numbers for sizing and positioning */
    const int keyboard_height = ver_res > hor_res ? ver_res / 3 : ver_res / 2;
    const int padding = keyboard_height / 8;
    const int label_width = hor_res - 2 * padding;

    ul_theme_apply(&(ul_themes_themes[0]));

    /* Main flexbox */
    lv_obj_t *container = lv_obj_create(lv_scr_act());
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(container, LV_PCT(100), ver_res - keyboard_height);
    lv_obj_set_pos(container, 0, 0);
    lv_obj_set_style_pad_row(container, padding, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(container, padding, LV_PART_MAIN);

    /* Label container */
    lv_obj_t *label_container = lv_obj_create(container);
    lv_obj_set_size(label_container, label_width, LV_PCT(100));
    lv_obj_set_flex_grow(label_container, 1);

    /* Top label */
    lv_obj_t *top_label_container = lv_obj_create(lv_scr_act());
    lv_obj_set_width(top_label_container, LV_PCT(100));
    lv_obj_set_height(top_label_container, LV_SIZE_CONTENT);
    lv_obj_set_align(top_label_container, LV_ALIGN_TOP_MID);

    /* Top label text */
    lv_obj_t *furios_label = lv_label_create(top_label_container);
    lv_label_set_text(furios_label, "FuriOS Terminal");
    lv_obj_align(furios_label, LV_ALIGN_TOP_MID, 0, 50);

    /* Terminal box */
    t_box = lv_textarea_create(lv_scr_act());
    static lv_style_t t_box_style;
    lv_style_init(&t_box_style);
    lv_style_set_bg_color(&t_box_style, lv_color_black());
    lv_style_set_text_color(&t_box_style, lv_color_white());
    lv_style_set_border_color(&t_box_style, lv_color_black());
    lv_obj_add_style(t_box, &t_box_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_refresh_style(t_box, LV_PART_MAIN,LV_STYLE_PROP_ANY);
    lv_obj_align(t_box, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_size(t_box, hor_res, ver_res-100-keyboard_height);
    lv_event_send(t_box, LV_EVENT_FOCUSED, NULL);

    /* Keyboard */
    keyboard = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, t_box);
    lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(keyboard, keyboard_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_set_pos(keyboard, 0, is_keyboard_hidden ? keyboard_height : 0);
    lv_obj_set_size(keyboard, hor_res, keyboard_height);
    ul_theme_prepare_keyboard(keyboard);

    toggle_keyboard_hidden();


    if (!ul_terminal_prepare_current_terminal((int)lv_obj_get_width(t_box),(int)lv_obj_get_height(t_box)))
        lv_textarea_add_text(t_box, "Could not prepare the terminal!");
       
    lv_timer_t* tty_update = lv_timer_create(update_tty_loop, 50, NULL);

    /* Run lvgl in "tickless" mode */
    uint32_t timeout = conf_opts.general.timeout * 1000; /* ms */
    while(1) {
        if (!timeout || lv_disp_get_inactive_time(NULL) < timeout) {
            lv_task_handler();
        } else if (timeout) {
            shutdown();
        }
        usleep(5000);
    }

    return 0;
}


/**
 * Tick generation
 */

/**
 * Generate tick for LVGL.
 * 
 * @return tick in ms
 */
uint32_t ul_get_tick(void) {
    static uint64_t start_ms = 0;
    if (start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    uint32_t time_ms = now_ms - start_ms;
    return time_ms;
}

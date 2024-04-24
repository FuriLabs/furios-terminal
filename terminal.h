/**
 * Copyright 2021 Johannes Marbach
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


#ifndef UL_TERMINAL_H
#define UL_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl/lvgl.h"
#include "lv_drv_conf.h"
#include "squeek2lvgl/sq2lv.h"

#define BUFFER_SIZE 4096

/**
 * Prepare the current TTY for graphics output.
 */
bool ul_terminal_prepare_current_terminal(void);

/**
 * Reset the current TTY to text output.
 */
void ul_terminal_reset_current_terminal(void);

/**
* Intepret keyboard input and add it to command buffer
*/
char* ul_terminal_update_interpret_buffer();

#endif /* UL_TERMINAL_H */

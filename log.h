/**
 * Copyright 2021 Johannes Marbach
 *
 * This file is part of unl0kr, hereafter referred to as the program.
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


#ifndef UL_LOG_H
#define UL_LOG_H

#include <stdio.h>

/**
 * Log levels
 */
typedef enum {
    /* Errors only */
    UL_LOG_LEVEL_ERROR   = 0,
    /* Include non-errors in log */
    UL_LOG_LEVEL_VERBOSE = 1
} ul_log_level;

/**
 * Set the log level.
 * 
 * @param level new log level value
 */
void ul_set_log_level(ul_log_level level);

/**
 * Log a message.
 * 
 * @param level log level of the message
 * @param format message format string
 * @param ... parameters to fill into the format string
 */
void ul_log(ul_log_level level, const char *format, ...);

/**
 * Handle an LVGL log message.
 * 
 * @param msg message to print
 */
void ul_print_cb(const char *msg);

#endif /* UL_LOG_H */
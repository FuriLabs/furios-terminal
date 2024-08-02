/**
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

#include <stdio.h>
#include <stdbool.h>

void remove_escape_codes(char *buffer)
{
    char *src = buffer;
    char *dst = buffer;
    bool inside_escape = false;

    while (*src != '\0') {
        if (*src == '\x1B') {
            inside_escape = true;
        } else if (*src == '^' && *(src + 1) == '@') {
            src++; // skip ^@, its garbage
        } else if (!inside_escape) {
            if (*src == '\x0A') {
                *dst++ = *src;
            } else {
                *dst++ = *src;
            }
        }

        if (inside_escape && (*src == '\x0D' || *src == 'h' || *src == 'm')) {
            inside_escape = false;
        }
        src++;
    }
    *dst = '\0';
}

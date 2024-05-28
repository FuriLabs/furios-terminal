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

void remove_escape_codes(char *buffer)
{
    char *src = buffer;
    char *dst = buffer;
    int inside_escape = 0;

    // Iterate through the string character by character
    while (*src != '\0') {
        if (*src == '\x1B') {  // Found ESC character, indicating start of escape sequence
            inside_escape = 1;
        }

        if (!inside_escape) {
            // Copy characters to destination if not inside an escape sequence
            *dst = *src;
            dst++;
        }

        if (inside_escape && (*src == 'h' || *src == 'm')) {  // Found end of escape sequence
            inside_escape = 0;
        }

        src++;  // Move to the next character
    }

    // Null-terminate the destination string
    *dst = '\0';
    
}
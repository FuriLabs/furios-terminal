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


#include "terminal.h"

#include "log.h"

#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>

#include <linux/kd.h>

#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>

/**
 * Static variables
 */

static int current_fd = -1;

static int original_mode = KD_TEXT;
static int original_kb_mode = K_UNICODE;

static char terminalBuffer[BUFFER_SIZE];

static int pid = 0;
static int ttyFD = 0;

bool termNeedsUpdate = false;

/**
 * Static prototypes
 */

/**
 * Close the current file descriptor and reopen /dev/tty0.
 * 
 * @return true if opening was successful, false otherwise
 */
static bool reopen_current_terminal(void);

/**
 * Close the current file descriptor.
 */
static void close_current_terminal(void);

static void* ttyThread(void* arg);


/**
 * Static functions
 */

static bool reopen_current_terminal(void) {
    close_current_terminal();

    current_fd = open("/dev/tty0", O_RDWR);
	if (current_fd < 0) {
        ul_log(UL_LOG_LEVEL_WARNING, "Could not open /dev/tty0");
		return false;
	}

    return true;
}

static void close_current_terminal(void) {
    if (current_fd < 0) {
        return;
    }

    close(current_fd);
    current_fd = -1;
}

static void* ttyThread(void* arg)
{
    struct winsize ws = {};
    ws.ws_col = 38;
    ws.ws_row = 38;
    pid = forkpty(&ttyFD, NULL, NULL, &ws);
    
    if (pid == 0) {
        char* args[] = { "/bin/sh", NULL };
        execve(args[0], args, NULL);
    }
    

    while (1) {
        struct pollfd p[2] = { { ttyFD, POLLIN, 0 } };
        int pollResult = poll(p, 2, 10);

        if (pollResult > 0) {
            read(ttyFD, &terminalBuffer, BUFFER_SIZE);
            terminalBuffer[BUFFER_SIZE] = '\0';
            termNeedsUpdate = true;
        }
        else {

        }
    }
}


/**
 * Public functions
 */

bool ul_terminal_prepare_current_terminal(void) {
    reopen_current_terminal();

    if (current_fd < 0) {
        ul_log(UL_LOG_LEVEL_WARNING, "Could not prepare current terminal");
        return false;
    }

    // NB: The order of calls appears to matter for some devices. See
    // https://gitlab.com/cherrypicker/unl0kr/-/issues/34 for further info.

    if (ioctl(current_fd, KDGKBMODE, &original_kb_mode) != 0) {
        ul_log(UL_LOG_LEVEL_WARNING, "Could not get terminal keyboard mode");
        return false;
    }

    if (ioctl(current_fd, KDSKBMODE, K_OFF) != 0) {
        ul_log(UL_LOG_LEVEL_WARNING, "Could not set terminal keyboard mode to off");
        return false;
    }

    if (ioctl(current_fd, KDGETMODE, &original_mode) != 0) {
        ul_log(UL_LOG_LEVEL_WARNING, "Could not get terminal mode");
        return false;
    }

    if (ioctl(current_fd, KDSETMODE, KD_GRAPHICS) != 0) {
        ul_log(UL_LOG_LEVEL_WARNING, "Could not set terminal mode to graphics");
        return false;
    }
    
    pthread_t ttyID;
    
    if (pthread_create(&ttyID, NULL, ttyThread, NULL) != 0){
        ul_log(UL_LOG_LEVEL_WARNING, "Could not start TTY thread");
        return false;
    }

    /*if (pthread_join(ttyID, NULL) != 0) {
        ul_log(UL_LOG_LEVEL_WARNING, "TTY thrad did not finish");
        return false;
    }*/

    return true;
}

void ul_terminal_reset_current_terminal(void) {
    if (current_fd < 0) {
        ul_log(UL_LOG_LEVEL_WARNING, "Could not reset current terminal");
        return;
    }

    // NB: The order of calls appears to matter for some devices. See
    // https://gitlab.com/cherrypicker/unl0kr/-/issues/34 for further info.

    if (ioctl(current_fd, KDSETMODE, original_mode) != 0) {
        ul_log(UL_LOG_LEVEL_WARNING, "Could not reset terminal mode");
    }

    if (ioctl(current_fd, KDSKBMODE, original_kb_mode) != 0) {
        ul_log(UL_LOG_LEVEL_WARNING, "Could not reset terminal keyboard mode");
    }

    close_current_terminal();
}

char* ul_terminal_update_interpret_buffer()
{
    return &terminalBuffer;
}

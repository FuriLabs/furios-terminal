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

#include "lvgl/src/widgets/keyboard/lv_keyboard_global.h"

#include "termstr.h"

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
#include <signal.h>

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

pthread_mutex_t ttyMutex;

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

static void runAndKillChildPids();

typedef struct termDimen
{
    int width;
    int height;
};

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


static void runKillChildPids()
{
    char numberBuffer[20];
    sprintf(numberBuffer,"%d",pid);
    
    char *commandToSend = (char*)malloc(strlen("pgrep -p ") + strlen(numberBuffer));
    strcpy(commandToSend,"pgrep -P ");
    strcpy(commandToSend+strlen("pgrep -p "),numberBuffer);
    FILE *fp = popen(commandToSend,"r");
    free(commandToSend);
    if (fp == NULL)
        return;
    
    while (fgets(numberBuffer,20,fp) != NULL)
        kill(atoi(numberBuffer),SIGINT);

    pclose(fp);
}

static void* ttyThread(void* arg)
{
    struct winsize ws = {};
    struct termDimen *ttyDimen = (struct termDimen*)arg;

    ws.ws_col = ttyDimen->width / 8; //max width of font_32
    ws.ws_row = ttyDimen->height / 16; //max height of font_32
    pid = forkpty(&ttyFD, NULL, NULL, &ws);

    char* enteredCommand = NULL;
    char* cutTerminal = NULL;
    int tmpLength = 0;
    
    if (pid == 0) {
        putenv("TERM=xterm");
        char* args[] = { getenv("SHELL"),"-l","-i", NULL};
        execl(args[0], args, NULL);
    }
    else {
        struct pollfd p[2] = { { ttyFD, POLLIN | POLLOUT, 0 } };
        while (1) {
            pthread_mutex_lock(&ttyMutex);
            poll(p, 2, 10);

            usleep(100);

            if (sigINTSent)
            {
                runKillChildPids();
                kill(pid,SIGINT);
                sigINTSent = false;
            }

            if ((p[0].revents & POLLIN) && !termNeedsUpdate) {
                int readValue = read(ttyFD, &terminalBuffer, BUFFER_SIZE);
                terminalBuffer[readValue] = '\0';
                
                if (tmpLength != 0) {
                    cutTerminal = (char*)malloc(tmpLength);
                    memcpy(cutTerminal, terminalBuffer, tmpLength);
                    cutTerminal[tmpLength] = '\0';
                }
    
                if (enteredCommand == NULL || cutTerminal == NULL || strlen(enteredCommand) == 0 || strcmp(enteredCommand,cutTerminal) != 0)
                    termNeedsUpdate = true;
                else if (strcmp(enteredCommand,cutTerminal) == 0){
                    removeEscapeCodes(terminalBuffer);
                    int copySize = strlen(terminalBuffer)-strlen(enteredCommand);
                    if (copySize-2 > 0){
                        memcpy(terminalBuffer,terminalBuffer+strlen(enteredCommand),copySize);
                        terminalBuffer[copySize] = 0;
                        termNeedsUpdate = true;
                    }
                }
                if ((enteredCommand != NULL) && (strlen(enteredCommand) > 0)) {
                    free(enteredCommand);
                    enteredCommand = NULL;
                }
                if (cutTerminal != NULL) {
                    free(cutTerminal);
                    cutTerminal = NULL;
                }
            }
            else if ((p[0].revents & POLLOUT) && commandReadyToSend) {
                write(ttyFD, &commandBuffer, sizeof(commandBuffer));
                commandReadyToSend = false;
                commandBufferPos = 0;
                enteredCommand = (char*)malloc(commandBufferLength);
                memcpy(enteredCommand, commandBuffer, commandBufferLength);
                enteredCommand[commandBufferLength] = '\0';
                for (long unsigned int i = 0; i < sizeof(commandBuffer); i++)
                    commandBuffer[i] = '\0';
                tmpLength = commandBufferLength;
                commandBufferLength = 0;
            }
            pthread_mutex_unlock(&ttyMutex);
        }
    }
}


/**
 * Public functions
 */

bool ul_terminal_prepare_current_terminal(int termWidth, int termHeight) {
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

    struct termDimen dimen;

    dimen.width = termWidth;
    dimen.height = termHeight;
    
    if (pthread_create(&ttyID, NULL, ttyThread, (void*)&dimen) != 0) {
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
    return (char*) &terminalBuffer;
}

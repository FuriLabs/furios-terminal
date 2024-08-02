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
#include <termios.h>

/**
 * Static variables
 */

static char terminal_buffer[BUFFER_SIZE];

static int pid = 0;
static int sig_int_pid = -1;
static int tty_fd = 0;

bool term_needs_update = false;

pthread_mutex_t tty_mutex;

/**
 * Static prototypes
 */

static void* tty_thread(void* arg);

static void run_kill_child_pids();

static void clean_command_buffer();

typedef struct term_dimen
{
    int width;
    int height;
} term_dimen;

/**
 * Static functions
 */

static void run_kill_child_pids()
{
    char number_buffer[20];
    sprintf(number_buffer,"%d",sig_int_pid);
    char *command_to_send = (char*)malloc(strlen("pgrep -P ") + strlen(number_buffer));
    strcpy(command_to_send,"pgrep -P ");
    strcpy(command_to_send+strlen("pgrep -P "),number_buffer);
    FILE *fp = popen(command_to_send,"r");
    free(command_to_send);
    if (fp == NULL)
        return;

    while (fgets(number_buffer,20,fp) != NULL)
        kill(atoi(number_buffer),SIGINT);

    pclose(fp);
}

static void clean_command_buffer()
{
    command_buffer_pos = 0;
    command_buffer_length = 0;
    for (long unsigned int i = 0; i < sizeof(command_buffer); i++)
        command_buffer[i] = '\0';
}

void disable_echo(int fd) {
    struct termios term;
    tcgetattr(fd, &term);
    term.c_lflag &= ~ECHO;
    tcsetattr(fd, TCSANOW, &term);
}

static void* tty_thread(void* arg)
{
    struct winsize ws = {0};
    struct term_dimen *tty_dimen = (struct term_dimen*)arg;

    ws.ws_col = tty_dimen->width / 8; //max width of font_32
    ws.ws_row = tty_dimen->height / 16; //max height of font_32
    pid = forkpty(&tty_fd, NULL, NULL, &ws);
    if (sig_int_pid < 0)
        sig_int_pid = pid;

    char* entered_command = NULL;
    char* cut_terminal = NULL;
    int tmp_length = 0;

    if (pid == 0) {
        putenv("TERM=xterm");
        char* shell = getenv("SHELL");
        if (shell == NULL) {
            shell = "/bin/sh";
        }
        char* args[] = { shell, "-l", "-i", NULL };
        execvp(args[0], args);
    } else {
        char* shell = getenv("SHELL");
        if (shell != NULL && strlen(shell) > 0) {
            disable_echo(tty_fd);
        }

        struct pollfd p[2] = { { tty_fd, POLLIN | POLLOUT, 0 } };
        while (1) {
            pthread_mutex_lock(&tty_mutex);
            poll(p, 2, 10);

            usleep(100);

            if (sig_int_sent)
            {
                run_kill_child_pids();
                kill(pid,SIGINT);
                sig_int_sent = false;
                clean_command_buffer();
            }

            if ((p[0].revents & POLLIN) && !term_needs_update) {
                int readValue = read(tty_fd, &terminal_buffer, BUFFER_SIZE);
                terminal_buffer[readValue] = '\0';

                if (tmp_length != 0) {
                    cut_terminal = (char*)malloc(tmp_length);
                    memcpy(cut_terminal, terminal_buffer, tmp_length);
                    cut_terminal[tmp_length] = '\0';
                }

                if (entered_command == NULL || cut_terminal == NULL || strlen(entered_command) == 0 || strcmp(entered_command,cut_terminal) != 0)
                    term_needs_update = true;
                else if (strcmp(entered_command, cut_terminal) == 0) {
                    remove_escape_codes(terminal_buffer);
                    for (size_t i = 0; i < strlen(terminal_buffer) - 1; i++)
                    {
                        if (terminal_buffer[i] == 0x5e && terminal_buffer[i + 1] == 0x40)
                        {
                            terminal_buffer[i] = '\n';
                            terminal_buffer[i+1] = '\n';
                        }
                    }
                    int copy_size = strlen(terminal_buffer) - strlen(entered_command);
                    if (copy_size-2 > 0){
                        memcpy(terminal_buffer,terminal_buffer+strlen(entered_command), copy_size);
                        terminal_buffer[copy_size] = 0;
                        term_needs_update = true;
                    }
                }
                if ((entered_command != NULL) && (strlen(entered_command) > 0)) {
                    free(entered_command);
                    entered_command = NULL;
                }
                if (cut_terminal != NULL) {
                    free(cut_terminal);
                    cut_terminal = NULL;
                }
            }
            else if ((p[0].revents & POLLOUT) && command_ready_to_send) {
                char first_word[5];
                sscanf(command_buffer, "%4s", first_word);
                if (strcmp(first_word, "exit") == 0) {
                    run_kill_child_pids();
                    kill(pid, SIGTERM);
                    close(tty_fd);
                    pthread_mutex_unlock(&tty_mutex);
                    exit(0);
                }

                write(tty_fd, command_buffer, strlen(command_buffer));
                command_ready_to_send = false;
                entered_command = strdup(command_buffer);
                tmp_length = strlen(command_buffer);
                clean_command_buffer();
            }
            pthread_mutex_unlock(&tty_mutex);
        }
    }

    return NULL;
}


/**
 * Public functions
 */

bool ul_terminal_prepare_current_terminal(int term_width, int term_height) {

    pthread_t tty_id;

    struct term_dimen dimen;

    dimen.width = term_width;
    dimen.height = term_height;

    if (pthread_create(&tty_id, NULL, tty_thread, (void*)&dimen) != 0) {
        ul_log(UL_LOG_LEVEL_WARNING, "Could not start TTY thread");
        return false;
    }

    return true;
}

char* ul_terminal_update_interpret_buffer()
{
    return (char*) &terminal_buffer;
}

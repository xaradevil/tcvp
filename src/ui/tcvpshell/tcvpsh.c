/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**/

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcvp_types.h>
#include <tcvpsh_tc2.h>

static player_t *player;
static pthread_mutex_t pmx = PTHREAD_MUTEX_INITIALIZER;

static int
tcvp_pause(char *p)
{
    static int paused = 0;

    pthread_mutex_lock(&pmx);
    if(player)
	(paused ^= 1)? player->stop(player): player->start(player);
    pthread_mutex_unlock(&pmx);

    return 0;
}

static int
tcvp_stop(char *p)
{
    player_t *pl;

    pthread_mutex_lock(&pmx);
    pl = player;
    player = NULL;
    pthread_mutex_unlock(&pmx);

    if(pl)
	pl->close(pl);

    return 0;
}

static int
tcvp_status(void *p, int state, uint64_t time)
{
/*     fprintf(stderr, "\r%li", time); */
    if(state == TCVP_STATE_END)
	tcvp_stop(NULL);

    return 0;
}

static int
tcvp_play(char *file)
{
    tcvp_stop(NULL);

    pthread_mutex_lock(&pmx);
    if((player = tcvp_open(file, tcvp_status, NULL)))
	player->start(player);
    pthread_mutex_unlock(&pmx);

    return 0;
}

static command *play_cmd, *pause_cmd, *stop_cmd;

extern int
tcvpsh_init(char *p)
{
    play_cmd = malloc(sizeof(command));
    play_cmd->name = strdup("play");
    play_cmd->cmd_fn = tcvp_play;
    shell_register_command(play_cmd);

    pause_cmd = malloc(sizeof(command));
    pause_cmd->name = strdup("pause");
    pause_cmd->cmd_fn = tcvp_pause;
    shell_register_command(pause_cmd);

    stop_cmd = malloc(sizeof(command));
    stop_cmd->name = strdup("stop");
    stop_cmd->cmd_fn = tcvp_stop;
    shell_register_command(stop_cmd);

    shell_register_prompt("TCVP$ ");

    return 0;
}

extern int
tcvpsh_shdn(void)
{
    tcvp_stop(NULL);
    shell_unregister_command(play_cmd);
    shell_unregister_command(pause_cmd);
    shell_unregister_command(stop_cmd);
    shell_unregister_prompt();

    return 0;
}

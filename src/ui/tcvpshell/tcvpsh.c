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

static eventq_t qs;

static int
tcvp_pause(char *p)
{
    tcvp_event_t *te = tcvp_alloc_event();
    te->type = TCVP_PAUSE;
    eventq_send(qs, te);
    tcfree(te);
    return 0;
}

static int
tcvp_stop(char *p)
{
    tcvp_event_t *te = tcvp_alloc_event();
    te->type = TCVP_CLOSE;
    eventq_send(qs, te);
    tcfree(te);

    return 0;
}

static int
tcvp_play(char *file)
{
    tcvp_open_event_t *te = tcvp_alloc_event();
    te->type = TCVP_OPEN;
    te->file = file;
    eventq_send(qs, te);
    tcfree(te);

    tcvp_pause(NULL);

    return 0;
}

static command *play_cmd, *pause_cmd, *stop_cmd;

extern int
tcvpsh_init(char *p)
{
    qs = eventq_new(NULL);
    eventq_attach(qs, "TCVP/control", EVENTQ_SEND);

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
    eventq_delete(qs);
    shell_unregister_command(play_cmd);
    shell_unregister_command(pause_cmd);
    shell_unregister_command(stop_cmd);
    shell_unregister_prompt();
    return 0;
}

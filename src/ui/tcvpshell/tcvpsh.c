/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcvp_types.h>
#include <tcvpsh_tc2.h>

static player_t *pl;
static eventq_t qs;

static int TCVP_OPEN;
static int TCVP_PAUSE;
static int TCVP_CLOSE;

static int
tcvp_pause(char *p)
{
    tcvp_event_send(qs, TCVP_PAUSE);
    return 0;
}

static int
tcvp_stop(char *p)
{
    tcvp_event_send(qs, TCVP_CLOSE);

    return 0;
}

static int
tcvp_play(char *file)
{
    tcvp_event_send(qs, TCVP_OPEN, file);

    tcvp_pause(NULL);

    return 0;
}

static command *play_cmd, *pause_cmd, *stop_cmd;

extern int
tcvpsh_init(char *p)
{
    char *qname, *qn;

    if(p){
	qname = strdup(p);
    } else {
	tcconf_section_t *cs = tcconf_new(NULL);
	pl = tcvp_new(cs);
	tcconf_getvalue(cs, "qname", "%s", &qname);
    }

    TCVP_OPEN = tcvp_event_get("TCVP_OPEN"); 
    TCVP_PAUSE = tcvp_event_get("TCVP_PAUSE");
    TCVP_CLOSE = tcvp_event_get("TCVP_CLOSE");

    qs = eventq_new(NULL);
    qn = alloca(strlen(qname) + 10);
    sprintf(qn, "%s/control", qname);
    eventq_attach(qs, qn, EVENTQ_SEND);
    free(qname);

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
    if(pl)
	pl->free(pl);

    shell_unregister_command(play_cmd);
    shell_unregister_command(pause_cmd);
    shell_unregister_command(stop_cmd);
    shell_unregister_prompt();

    return 0;
}

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
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <tcvp_types.h>
#include <tcvp_tc2.h>

static pthread_t play_thr;

static int nfiles;
char **files;
sem_t psm;

static void
sigint(int s)
{
    sem_post(&psm);
}

static int
tcvp_update(void *p, int state, uint64_t time)
{
    if(state == TCVP_STATE_END)
	sem_post(&psm);
    return 0;
}

static void *
tcvp_play(void *p)
{
    conf_section *cf;
    int i;

    cf = conf_new(NULL);
    sem_init(&psm, 0, 0);

    for(i = 0; i < nfiles; i++){
	player_t *pl;

	if(!(pl = tcvp_open(files[i], tcvp_update, NULL, cf)))
	    continue;

	pl->start(pl);
	sem_wait(&psm);
	pl->close(pl);
    }

    conf_free(cf);

    tc2_request(TC2_UNLOAD_MODULE, 0, "TCVP/cmdline");
    return NULL;
}

extern int
tcvp_init(char *p)
{
    pthread_create(&play_thr, NULL, tcvp_play, NULL);
    return 0;
}

extern int
tcvp_stop(void)
{
    pthread_join(play_thr, NULL);
}

extern int
main(int argc, char **argv)
{
    struct sigaction sa;

    nfiles = argc - 1;
    files = argv + 1;

    sa.sa_handler = sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    tc2_add_config(TCVP_CONF);
    tc2_init();
    tc2_request(TC2_ADD_MODULE, 0, NULL, &tc2__module_info);
    tc2_request(TC2_LOAD_MODULE, 0, "TCVP/cmdline", NULL);
    tc2_run();
    return 0;
}

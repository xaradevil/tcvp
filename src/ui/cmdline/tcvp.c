/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

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
#include <getopt.h>
#include <tcstring.h>
#include <tctypes.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <tcvp_types.h>
#include <tcvp_tc2.h>

static pthread_t play_thr, evt_thr;

static int nfiles;
static char **files;
static sem_t psm;
static int intr;
static conf_section *cf;
static int validate;

static void
sigint(int s)
{
    if(intr)
	return;

    intr = 1;
    sem_post(&psm);
}

static void *
tcl_event(void *p)
{
    eventq_t qr = eventq_new(tcref);
    eventq_attach(qr, "TCVP", EVENTQ_RECV);

    for(;;){
	tcvp_event_t *te = eventq_recv(qr);
	switch(te->type){
	case TCVP_STATE:
	    switch(te->state.state){
	    case TCVP_STATE_ERROR:
		printf("Error opening file.\n");
	    case TCVP_STATE_END:
		sem_post(&psm);
	    }
	    break;
	}
	tcfree(te);
    }
    return NULL;
}

static void *
tcl_play(void *p)
{
    int i;
    player_t *pl = tcvp_new(cf);
    eventq_t qs = eventq_new(NULL);

    eventq_attach(qs, "TCVP", EVENTQ_SEND);
    sem_init(&psm, 0, 0);

    for(i = 0; i < nfiles; i++){
	intr = 0;

	if(validate){
	    stream_validate(files[i], cf);
	} else {
	    tcvp_open_event_t *te = tcvp_alloc_event();
	    te->type = TCVP_OPEN;
	    te->file = files[i];
	    eventq_send(qs, te);
	    tcfree(te);

	    printf("Playing \"%s\"...\n", files[i]);
	    pl->start(pl);
	    sem_wait(&psm);
	    pl->close(pl);
	    if(intr){
		sem_wait(&psm);
	    }
	}
    }

    pl->free(pl);
    eventq_delete(qs);

    tc2_request(TC2_UNLOAD_MODULE, 0, "TCVP/cmdline");
    return NULL;
}

extern int
tcl_init(char *p)
{
    pthread_create(&evt_thr, NULL, tcl_event, NULL);
    pthread_create(&play_thr, NULL, tcl_play, NULL);

    return 0;
}

extern int
tcl_stop(void)
{
    pthread_join(play_thr, NULL);
    pthread_cancel(evt_thr);
    pthread_join(evt_thr, NULL);

    return 0;
}

static void
show_help(void)
{
    /* FIXME: better helpscreen */
    printf("TCVP help\n"
	   "   -h      --help              This helpscreen\n"
	   "   -A dev  --audio-device=dev  Select audio device\n"
	   "   -V dev  --video-device=dev  Select video device\n"
	   "   -a #    --audio-stream=#    Select audio stream\n"
	   "   -v #    --video-stream=#    Select video stream\n"
	   "   -C      --validate          Check file integrity\n"
	   "   -s t    --seek=t            Seek t seconds at start\n");
}

static int
parse_options(int argc, char **argv)
{
    struct option long_options[] = {
	{"help", no_argument, 0, 'h'},
	{"audio-device", required_argument, 0, 'A'},
	{"audio-stream", required_argument, 0, 'a'},
	{"video-device", required_argument, 0, 'V'},
	{"video-stream", required_argument, 0, 'v'},
	{"validate", no_argument, 0, 'C'},
	{"seek", required_argument, 0, 's'},
	{0, 0, 0, 0}
    };

    for(;;){
	int c, option_index = 0;
     
	c = getopt_long(argc, argv, "hA:a:V:v:Cs:",
			long_options, &option_index);
	
	if(c == -1)
	    break;

	switch(c){
	case 'h':
	    show_help();
	    exit(0);
	    break;

	case 'A':
	    conf_setvalue(cf, "audio/device", "%s", optarg);
	    break;

	case 'a':
	    conf_setvalue(cf, "audio/stream", "%i", strtol(optarg, NULL, 0));
	    break;

	case 'V':
	    conf_setvalue(cf, "video/device", "%s", optarg);
	    break;

	case 'v':
	    conf_setvalue(cf, "video/stream", "%i", strtol(optarg, NULL, 0));
	    break;

	case 'C':
	    validate = 1;
	    break;

	case 's':
	    conf_setvalue(cf, "start_time", "%i", strtol(optarg, NULL, 0));
	    break;
	}
    }

    return optind;
}

extern int
main(int argc, char **argv)
{
    struct sigaction sa;
    int opt_num;

    cf = conf_new(NULL);

    opt_num = parse_options(argc, argv);

    if(argc == opt_num) {
	show_help();
	return 0;
    }

    nfiles = argc - opt_num;
    files = argv + opt_num;

    if(!validate){
	sa.sa_handler = sigint;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGINT, &sa, NULL);
    }

    tc2_add_config(TCVP_CONF);
    tc2_init();
    tc2_request(TC2_ADD_MODULE, 0, NULL, &tc2__module_info);
    tc2_request(TC2_LOAD_MODULE, 0, "TCVP/cmdline", NULL);
    tc2_run();

    conf_free(cf);

    return 0;
}

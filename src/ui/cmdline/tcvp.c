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
static player_t *pl;
static eventq_t qr;
static char *qname;

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
    int r = 1;
    while(r){
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
	case -1:
	    r = 0;
	    break;
	}
	tcfree(te);
    }
    return NULL;
}

static void *
tcl_play(void *p)
{
    eventq_t qs = eventq_new(NULL);
    char qn[strlen(qname)+10];
    int i;

    sprintf(qn, "%s/control", qname);
    eventq_attach(qs, qn, EVENTQ_SEND);
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

    eventq_delete(qs);

    tc2_request(TC2_UNLOAD_MODULE, 0, MODULE_INFO.name);
    return NULL;
}

extern int
tcl_init(char *p)
{
    pl = tcvp_new(cf);
    conf_getvalue(cf, "qname", "%s", &qname);
    if(nfiles){
	char qn[strlen(qname)+8];
	qr = eventq_new(tcref);
	sprintf(qn, "%s/status", qname);
	eventq_attach(qr, qn, EVENTQ_RECV);
	pthread_create(&evt_thr, NULL, tcl_event, NULL);
	pthread_create(&play_thr, NULL, tcl_play, NULL);
    } else if(tcvp_ui_cmdline_conf_ui){
	char *ui = alloca(strlen(tcvp_ui_cmdline_conf_ui) + 9);
	sprintf(ui, "TCVP/ui/%s", tcvp_ui_cmdline_conf_ui);
	tc2_request(TC2_LOAD_MODULE, 1, ui, qname);
    } else {
	show_help();
	tc2_request(TC2_UNLOAD_MODULE, 0, MODULE_INFO.name);
    }

    return 0;
}

extern int
tcl_stop(void)
{
    pthread_join(play_thr, NULL);

    if(qr){
	tcvp_event_t *te = tcvp_alloc_event();
	te->type = -1;
	eventq_send(qr, te);
	tcfree(te);
	pthread_join(evt_thr, NULL);
    }

    pl->free(pl);

    return 0;
}

/* Identifiers for long-only options */
#define OPT_TC2_DEBUG 128
#define OPT_TC2_VERBOSE 129

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
	{"tc2-debug", required_argument, 0, OPT_TC2_DEBUG},
	{"tc2-verbose", required_argument, 0, OPT_TC2_VERBOSE},
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

	case OPT_TC2_DEBUG:
	    tc2_debug(strtol(optarg, NULL, 0));
	    break;

	case OPT_TC2_VERBOSE:
	    tc2_verbose(strtol(optarg, NULL, 0));
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
    tc2_request(TC2_ADD_MODULE, 0, NULL, &MODULE_INFO);
    tc2_request(TC2_LOAD_MODULE, 0, MODULE_INFO.name, NULL);
    tc2_run();

    conf_free(cf);

    return 0;
}

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
#include <tcalloc.h>
#include <sys/time.h>
#include <tcvp_types.h>
#include <tcvp_event.h>
#include <tcvp_tc2.h>

static pthread_t check_thr, evt_thr, intr_thr;

static int nfiles;
static char **files;
static char *playlist;
static sem_t psm;
static int intr;
static conf_section *cf;
static int validate;
static player_t *pl;
static eventq_t qr, qs;
static char *qname;
static char *sel_ui;
static playlist_t *pll;
static int shuffle;

static void
show_help(void)
{
    /* FIXME: better helpscreen */
    printf("TCVP help\n"
	   "   -h      --help                This helpscreen\n"
	   "   -A dev  --audio-device=dev    Select audio device\n"
	   "   -V dev  --video-device=dev    Select video device\n"
	   "   -a #    --audio-stream=#      Select audio stream\n"
	   "   -v #    --video-stream=#      Select video stream\n"
	   "   -C      --validate            Check file integrity\n"
	   "   -s t    --seek=t              Seek t seconds at start\n"
	   "   -u name --user-interface=name Select user interface\n"
	   "   -z      --shuffle             Shuffle files\n"
	   "   -@ file --playlist=file       Load playlist from file\n"
	   "   -f      --fullscreen          Fill entire screen\n"
	);
}

static void
sigint(int s)
{
    if(pthread_self() == intr_thr){
	sem_post(&psm);
    }
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
		break;
	    case TCVP_STATE_PL_END:
		tc2_request(TC2_UNLOAD_ALL, 0);
		break;
	    }
	    break;
	case TCVP_LOAD:
	    printf("Loaded \"%s\"\n",
		   te->load.stream->title?: te->load.stream->file);
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
tcl_intr(void *p)
{
    struct timeval tv;
    uint64_t lt = 0;

    while(intr){
	uint64_t t;

	sem_wait(&psm);
	if(!intr)
	    break;

	gettimeofday(&tv, NULL);
	t = 1000000LL * tv.tv_sec + tv.tv_usec;
	
	if(t - lt < 200000){
	    tc2_request(TC2_UNLOAD_ALL, 0);
	} else {
	    tcvp_event_t *te;
	    te = tcvp_alloc_event(TCVP_PL_NEXT);
	    eventq_send(qs, te);
	    tcfree(te);
	}
	lt = t;
    }

    return NULL;
}

static void *
tcl_check(void *p)
{
    int i;

    for(i = 0; i < nfiles; i++){
	if(nfiles > 1)
	    printf("checking \"%s\"...\n", files[i]);
	stream_validate(files[i], cf);
    }

    tc2_request(TC2_UNLOAD_MODULE, 0, MODULE_INFO.name);
    return NULL;
}

extern int
tcl_init(char *p)
{
    pl = tcvp_new(cf);
    conf_getvalue(cf, "qname", "%s", &qname);

    if(validate){
	pthread_create(&check_thr, NULL, tcl_check, NULL);
	return 0;
    }

    if(!sel_ui)
	sel_ui = tcvp_ui_cmdline_conf_ui;

    pll = playlist_new(cf);
    pll->add(pll, files, nfiles, 0);
    if(playlist)
	nfiles += pll->addlist(pll, playlist, nfiles);
    if(shuffle)
	pll->shuffle(pll, 0, nfiles);

    if(sel_ui){
	char *ui = alloca(strlen(sel_ui)+9);
	sprintf(ui, "TCVP/ui/%s", sel_ui);
	tc2_request(TC2_LOAD_MODULE, 1, ui, qname);
	tc2_request(TC2_ADD_DEPENDENCY, 1, ui);
	tc2_request(TC2_UNLOAD_MODULE, 1, ui);
    } else if(nfiles) {
	char qn[strlen(qname)+9];
	struct sigaction sa;
	tcvp_event_t *te;

	qr = eventq_new(tcref);
	sprintf(qn, "%s/status", qname);
	eventq_attach(qr, qn, EVENTQ_RECV);
	pthread_create(&evt_thr, NULL, tcl_event, NULL);

	qs = eventq_new(NULL);
	sprintf(qn, "%s/control", qname);
	eventq_attach(qs, qn, EVENTQ_SEND);
	te = tcvp_alloc_event(TCVP_PL_START);
	eventq_send(qs, te);
	tcfree(te);

	sem_init(&psm, 0, 0);
	sa.sa_handler = sigint;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGINT, &sa, NULL);
	intr = 1;
	pthread_create(&intr_thr, NULL, tcl_intr, NULL);
    } else {
	show_help();
	tc2_request(TC2_UNLOAD_ALL, 0);
    }

    return 0;
}

extern int
tcl_stop(void)
{
    if(validate)
	pthread_join(check_thr, NULL);

    if(qr){
	tcvp_event_t *te = tcvp_alloc_event(-1);
	eventq_send(qr, te);
	tcfree(te);
	pthread_join(evt_thr, NULL);

	intr = 0;
	sem_post(&psm);
	pthread_join(intr_thr, NULL);
	eventq_delete(qs);
	eventq_delete(qr);
	sem_destroy(&psm);
    }

    if(pl)
	pl->free(pl);

    if(pll)
	pll->free(pll);

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
	{"user-interface", required_argument, 0, 'u'},
	{"tc2-debug", required_argument, 0, OPT_TC2_DEBUG},
	{"tc2-verbose", required_argument, 0, OPT_TC2_VERBOSE},
	{"shuffle", no_argument, 0, 'z'},
	{"playlist", required_argument, 0, '@'},
	{"fullscreen", required_argument, 0, 'f'},
	{0, 0, 0, 0}
    };

    for(;;){
	int c, option_index = 0, s;
	char *ot;
     
	c = getopt_long(argc, argv, "hA:a:V:v:Cs:u:z@:f",
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
	    s = strtol(optarg, &ot, 0);
	    if(*ot)
		s = -1;
	    conf_setvalue(cf, "audio/stream", "%i", s);
	    break;

	case 'V':
	    conf_setvalue(cf, "video/device", "%s", optarg);
	    break;

	case 'v':
	    s = strtol(optarg, &ot, 0);
	    if(*ot)
		s = -1;
	    conf_setvalue(cf, "video/stream", "%i", s);
	    break;

	case 'C':
	    validate = 1;
	    break;

	case 's':
	    conf_setvalue(cf, "start_time", "%i", strtol(optarg, NULL, 0));
	    break;

	case 'u':
	    sel_ui = optarg;
	    break;

	case 'z':
	    shuffle = 1;
	    break;

	case '@':
	    playlist = optarg;
	    break;

	case 'f':
	    conf_setvalue(cf, "video/fullscreen", "%i", 1);
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
    int opt_num;

    cf = conf_new(NULL);

    opt_num = parse_options(argc, argv);

    nfiles = argc - opt_num;
    files = argv + opt_num;

    tc2_add_config(TCVP_CONF);
    tc2_init();
    tc2_request(TC2_ADD_MODULE, 0, NULL, &MODULE_INFO);
    tc2_request(TC2_LOAD_MODULE, 0, MODULE_INFO.name, NULL);
    tc2_run();

    conf_free(cf);

    return 0;
}

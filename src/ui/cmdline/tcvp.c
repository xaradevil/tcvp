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
#include <mcheck.h>
#include <sys/time.h>
#include <tcvp_types.h>
#include <tcvp_tc2.h>

static pthread_t check_thr, evt_thr, intr_thr;

static int nfiles;
static char **files;
static char *playlist;
static sem_t psm;
static int intr;
static tcconf_section_t *cf;
static int validate;
static player_t *pl;
static eventq_t qr, qs;
static char *sel_ui;
static playlist_t *pll;
static int shuffle;

static int TCVP_STATE;
static int TCVP_LOAD;
static int TCVP_PL_START;
static int TCVP_PL_NEXT;

typedef union tcvp_cl_event {
    int type;
    tcvp_state_event_t state;
    tcvp_load_event_t load;
} tcvp_cl_event_t;

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
	   "   -t t    --time=t              Play t seconds\n"
	   "   -u name --user-interface=name Select user interface\n"
	   "   -z      --shuffle             Shuffle files\n"
	   "   -@ file --playlist=file       Load playlist from file\n"
	   "   -f      --fullscreen          Fill entire screen\n"
	   "           --aspect=a[/b]        Force video aspect ratio\n"
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
	tcvp_cl_event_t *te = eventq_recv(qr);
	if(te->type == TCVP_STATE){
	    switch(te->state.state){
	    case TCVP_STATE_ERROR:
		printf("Error opening file.\n");
		break;
	    case TCVP_STATE_PL_END:
		tc2_request(TC2_UNLOAD_ALL, 0);
		break;
	    }
	} else if(te->type == TCVP_LOAD){
	    printf("Loaded \"%s\"\n",
		   (char *)(tcattr_get(te->load.stream, "title")?:
			    tcattr_get(te->load.stream, "file")));
	} else if(te->type == -1){
	    r = 0;
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
    int ic = 0;

    while(intr){
	uint64_t t;

	sem_wait(&psm);
	if(!intr)
	    break;

	gettimeofday(&tv, NULL);
	t = 1000000LL * tv.tv_sec + tv.tv_usec;
	
	if(t - lt < 500000)
	    ic++;

	switch(ic){
	case 0:
	    tcvp_event_send(qs, TCVP_PL_NEXT);
	    break;
	case 1:
	    tc2_request(TC2_UNLOAD_ALL, 0);
	    break;
	case 2:
	    exit(0);
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
    char *qname;
    pl = tcvp_new(cf);
    tcconf_getvalue(cf, "qname", "%s", &qname);

    if(validate){
	pthread_create(&check_thr, NULL, tcl_check, NULL);
	return 0;
    }

    TCVP_STATE = tcvp_event_get("TCVP_STATE");
    TCVP_LOAD = tcvp_event_get("TCVP_LOAD");
    TCVP_PL_START = tcvp_event_get("TCVP_PL_START");
    TCVP_PL_NEXT = tcvp_event_get("TCVP_PL_NEXT");

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

	qr = eventq_new(tcref);
	sprintf(qn, "%s/status", qname);
	eventq_attach(qr, qn, EVENTQ_RECV);
	pthread_create(&evt_thr, NULL, tcl_event, NULL);

	qs = eventq_new(NULL);
	sprintf(qn, "%s/control", qname);
	eventq_attach(qs, qn, EVENTQ_SEND);

	tcvp_event_send(qs, TCVP_PL_START);

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

    free(qname);
    return 0;
}

extern int
tcl_stop(void)
{
    if(validate)
	pthread_join(check_thr, NULL);

    if(pl)
	pl->free(pl);

    if(pll)
	pll->free(pll);

    if(qr){
	tcvp_event_send(qr, -1);
	pthread_join(evt_thr, NULL);

	intr = 0;
	sem_post(&psm);
	pthread_join(intr_thr, NULL);
	eventq_delete(qs);
	eventq_delete(qr);
	sem_destroy(&psm);
    }

    return 0;
}

/* Identifiers for long-only options */
#define OPT_TC2_DEBUG 128
#define OPT_TC2_VERBOSE 129
#define OPT_TRACE_MALLOC 130
#define OPT_ASPECT 131

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
	{"time", required_argument, 0, 't'},
	{"user-interface", required_argument, 0, 'u'},
	{"tc2-debug", required_argument, 0, OPT_TC2_DEBUG},
	{"tc2-verbose", required_argument, 0, OPT_TC2_VERBOSE},
	{"shuffle", no_argument, 0, 'z'},
	{"playlist", required_argument, 0, '@'},
	{"fullscreen", required_argument, 0, 'f'},
	{"aspect", required_argument, 0, OPT_ASPECT},
	{"output", required_argument, 0, 'o'},
	{"profile", required_argument, 0, 'P'},
	{"trace-malloc", no_argument, 0, OPT_TRACE_MALLOC},
	{0, 0, 0, 0}
    };

    for(;;){
	int c, option_index = 0, s;
	char *ot;
     
	c = getopt_long(argc, argv, "hA:a:V:v:Cs:u:z@:fo:P:t:",
			long_options, &option_index);
	
	if(c == -1)
	    break;

	switch(c){
	case 'h':
	    show_help();
	    exit(0);
	    break;

	case 'A':
	    tcconf_setvalue(cf, "audio/device", "%s", optarg);
	    break;

	case 'a':
	    s = strtol(optarg, &ot, 0);
	    if(*ot)
		s = -1;
	    tcconf_setvalue(cf, "audio/stream", "%i", s);
	    break;

	case 'V':
	    tcconf_setvalue(cf, "video/device", "%s", optarg);
	    break;

	case 'v':
	    s = strtol(optarg, &ot, 0);
	    if(*ot)
		s = -1;
	    tcconf_setvalue(cf, "video/stream", "%i", s);
	    break;

	case 'C':
	    validate = 1;
	    break;

	case 's':
	    tcconf_setvalue(cf, "start_time", "%i", strtol(optarg, NULL, 0));
	    break;

	case 't':
	    tcconf_setvalue(cf, "play_time", "%i", strtol(optarg, NULL, 0));
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
	    tcconf_setvalue(cf, "video/fullscreen", "%i", 1);
	    break;

	case OPT_ASPECT: {
	    float a;
	    char *t;
	    a = strtod(optarg, &t);
	    if(*t++ == '/')
		a /= strtod(t, NULL);
	    tcconf_setvalue(cf, "video/aspect", "%f", a);
	    break;
	}

	case 'o':
	    tcconf_setvalue(cf, "outname", "%s", optarg);
	    break;

	case 'P':
	    tcconf_setvalue(cf, "profile", "%s", optarg);
	    break;

	case OPT_TC2_DEBUG:
	    tc2_debug(strtol(optarg, NULL, 0));
	    break;

	case OPT_TC2_VERBOSE:
	    tc2_verbose(strtol(optarg, NULL, 0));
	    break;

	case OPT_TRACE_MALLOC:
	    mtrace();
	}
    }

    return optind;
}

extern int
main(int argc, char **argv)
{
    int opt_num;
    char userconf[1024];

    cf = tcconf_new(NULL);

    opt_num = parse_options(argc, argv);

    nfiles = argc - opt_num;
    files = argv + opt_num;

    snprintf(userconf, 1024, "%s/.tcvp/tcvp.conf", getenv("HOME"));

    tc2_add_config(TCVP_CONF);
    tc2_add_config(userconf);
    tc2_init();
    tc2_request(TC2_ADD_MODULE, 0, NULL, &MODULE_INFO);
    tc2_request(TC2_LOAD_MODULE, 0, MODULE_INFO.name, NULL);
    tc2_run();
    tc2_free();

    tcfree(cf);

    return 0;
}

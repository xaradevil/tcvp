/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <tcstring.h>
#include <tctypes.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <tcalloc.h>
#include <mcheck.h>
#include <sys/time.h>
#include <limits.h>
#include <tcvp_types.h>
#include <tcvp_tc2.h>

static pthread_t check_thr, evt_thr, intr_thr;

static int nfiles, npl;
static char **files;
static char **playlist;
static sem_t psm;
static int intr;
static tcconf_section_t *cf;
static int validate;
static player_t *pl;
static eventq_t qr, qs;
static char *sel_ui;
static playlist_t *pll;
static int shuffle;
static char *skin;
static int prl;
static char **aonames, **nanames;
static int nadd, nnadd;
static tcvp_addon_t **addons;
static char **commands;
static int ncmds;
static int isdaemon;

static int TCVP_STATE;
static int TCVP_PL_START;
static int TCVP_PL_NEXT;
static int TCVP_PL_ADD;
static int TCVP_PL_ADDLIST;
static int TCVP_PL_SHUFFLE;
static int TCVP_OPEN_MULTI;
static int TCVP_START;

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
	   "   -h      --help                This text\n"
	   "   -A dev  --audio-device=dev    Select audio device\n"
	   "   -V dev  --video-device=dev    Select video device\n"
	   "   -a #    --audio-stream=#      Select audio stream\n"
	   "   -v #    --video-stream=#      Select video stream\n"
	   "   -C      --validate            Check file integrity\n"
	   "   -s t    --seek=t              Seek t seconds at start\n"
	   "   -t t    --time=t              Play t seconds\n"
	   "   -u name --user-interface=name Select user interface\n"
	   "   -z      --shuffle             Enable shuffle\n"
	   "   -Z      --noshuffle           Disable shuffle\n"
	   "   -@ file --playlist=file       Load playlist from file\n"
	   "   -f      --fullscreen          Fill entire screen\n"
	   "           --aspect=a[/b]        Force video aspect ratio\n"
	   "           --skin=file           Select skin\n"
	   "   -D      --daemon              Fork into background.\n"
	   "   -x name --addon=name          Enable addon.\n"
	   "   -X name --noaddon=name        Disable addon.\n"
	   "           --play\n"
	   "           --pause\n"
	   "           --stop\n"
	   "           --next\n"
	   "           --prev\n"
	);
}

static void
add_addon(char *ao)
{
    int i;
    for(i = 0; i < nadd; i++)
	if(!strcmp(aonames[i], ao))
	    return;
    aonames = realloc(aonames, (nadd + 1) * sizeof(*aonames));
    aonames[nadd++] = strdup(ao);
}

static int
noaddon(char *ao)
{
    int i;
    for(i = 0; i < nnadd; i++)
	if(!strcmp(nanames[i], ao))
	    return 1;
    return 0;
}

static void
add_cmd(char *cmd)
{
    commands = realloc(commands, (ncmds + 1) * sizeof(*commands));
    commands[ncmds++] = cmd;
}

static char *
fullpath(char *file)
{
    char *path, *p;

    if(file[0] == '/' || strchr(file, ':'))
	return strdup(file);

    path = malloc(PATH_MAX);
    getcwd(path, PATH_MAX);
    p = strchr(path, 0);
    snprintf(p, PATH_MAX - (p - path), "/%s", file);

    return path;
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
	    case TCVP_STATE_END:
		if(!prl)
		    break;
	    case TCVP_STATE_PL_END:
		if(!isdaemon && !sel_ui)
		    tc2_request(TC2_UNLOAD_ALL, 0);
		break;
	    }
	} else if(te->type == -1){
	    r = 0;
	}
	tcfree(te);
    }
    return NULL;
}

/* There is a race condition here, but I don't care. */
static int sig;

static void
sigint(int s)
{
    if(pthread_self() == intr_thr){
	sig = s;
	sem_post(&psm);
    }
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
	    if(sig == SIGINT && !prl && !sel_ui){
		tcvp_event_send(qs, TCVP_PL_NEXT);
		break;
	    }
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
    char *qname = NULL, *qn;
    struct sigaction sa;
    tcconf_section_t *tcvp_conf, *prconf = NULL;
    char *profile = NULL;
    int i;

    for(i = 0; i < nfiles; i++)
	files[i] = fullpath(files[i]);
    for(i = 0; i < npl; i++)
	playlist[i] = fullpath(playlist[i]);

    if(validate){
	pthread_create(&check_thr, NULL, tcl_check, NULL);
	return 0;
    }

    if(!sel_ui)
	sel_ui = tcvp_ui_cmdline_conf_ui;
    if(sel_ui && !strcmp(sel_ui, "none"))
	sel_ui = NULL;

    if(!ncmds && !nfiles && !npl && !sel_ui && !isdaemon && !shuffle){
	show_help();
	tc2_request(TC2_UNLOAD_ALL, 0);
	return 0;
    }

    tcvp_conf = tc2_get_conf("TCVP");
    if(tcconf_getvalue(cf, "profile", "%s", &profile) <= 0)
	tcconf_getvalue(tcvp_conf, "default_profile", "%s", &profile);
    if(profile){
	char pr[strlen(profile) + 12];
	sprintf(pr, "profiles/%s", profile);
	if((prconf = tcconf_getsection(tcvp_conf, pr))){
	    void *nv = NULL;
	    char *aon;
	    while(tcconf_nextvalue(prconf, "addon", &nv, "%s", &aon) > 0){
		add_addon(aon);
		free(aon);
	    }
	    tcfree(prconf);
	}
	free(profile);
    }
    tcfree(tcvp_conf);

    aonames = realloc(aonames, (nadd + tcvp_ui_cmdline_conf_addon_count) *
		      sizeof(*aonames));
    memcpy(aonames + nadd, tcvp_ui_cmdline_conf_addon,
	   tcvp_ui_cmdline_conf_addon_count * sizeof(*aonames));
    nadd += tcvp_ui_cmdline_conf_addon_count;
    addons = calloc(nadd, sizeof(*addons));
    for(i = 0; i < nadd; i++){
	char an[strlen(aonames[i]) + 16];
	tcvp_addon_new_t anf;

	if(noaddon(aonames[i]))
	    continue;

	sprintf(an, "tcvp/addon/%s", aonames[i]);
	anf = tc2_get_symbol(an, "new");
	if(anf){
	    addons[i] = anf(cf);
	    if(!qname)
		tcconf_getvalue(cf, "qname", "%s", &qname);
	}
    }

    if(!qname){
	pl = tcvp_new(cf);
	pll = playlist_new(cf);
	tcconf_getvalue(cf, "qname", "%s", &qname);
    } else {
	/* Make sure the event types get registered */
	/* FIXME: do it in a nicer way */
	tc2_request(TC2_LOAD_MODULE, 1, "tcvp", NULL);
	tc2_request(TC2_ADD_DEPENDENCY, 1, "tcvp");
	tc2_request(TC2_UNLOAD_MODULE, 1, "tcvp");

	tc2_request(TC2_LOAD_MODULE, 1, "playlist", NULL);
	tc2_request(TC2_ADD_DEPENDENCY, 1, "playlist");
	tc2_request(TC2_UNLOAD_MODULE, 1, "playlist");
    }

    for(i = 0; i < nadd; i++)
	if(addons[i])
	    addons[i]->init(addons[i]);

    TCVP_STATE = tcvp_event_get("TCVP_STATE");
    TCVP_PL_START = tcvp_event_get("TCVP_PL_START");
    TCVP_PL_NEXT = tcvp_event_get("TCVP_PL_NEXT");
    TCVP_PL_ADD = tcvp_event_get("TCVP_PL_ADD");
    TCVP_PL_ADDLIST = tcvp_event_get("TCVP_PL_ADDLIST");
    TCVP_PL_SHUFFLE = tcvp_event_get("TCVP_PL_SHUFFLE");
    TCVP_OPEN_MULTI = tcvp_event_get("TCVP_OPEN_MULTI");
    TCVP_START = tcvp_event_get("TCVP_START");

    qn = alloca(strlen(qname)+9);
    qs = eventq_new(NULL);
    sprintf(qn, "%s/control", qname);
    eventq_attach(qs, qn, EVENTQ_SEND);

    if(!prl){
	if(nfiles)
	    tcvp_event_send(qs, TCVP_PL_ADD, files, nfiles, -1);
	for(i = 0; i < npl; i++)
	    tcvp_event_send(qs, TCVP_PL_ADDLIST, playlist[i], -1);
    } else {
	tcvp_event_send(qs, TCVP_OPEN_MULTI, nfiles, files);
    }

    if(shuffle > 0)
	tcvp_event_send(qs, TCVP_PL_SHUFFLE, 1);
    else if(shuffle < 0)
	tcvp_event_send(qs, TCVP_PL_SHUFFLE, 0);

    if(!ncmds && tcvp_ui_cmdline_conf_autoplay){
	if(prl){
	    tcvp_event_send(qs, TCVP_START);
	} else {
	    tcvp_event_send(qs, TCVP_PL_START);
	}
    }

    if(!ncmds && sel_ui){
	char *ui = alloca(strlen(sel_ui)+9);
	char *op = alloca(strlen(qname) + 10 + (skin? (strlen(skin) + 10): 0));
	char *p = op;
	sprintf(ui, "TCVP/ui/%s", sel_ui);
	p += sprintf(p, "qname '%s';", qname);
	if(skin)
	    sprintf(p, "skin '%s';", skin);
	tc2_request(TC2_LOAD_MODULE, 1, ui, op);
	tc2_request(TC2_ADD_DEPENDENCY, 1, ui);
	tc2_request(TC2_UNLOAD_MODULE, 1, ui);
    }

    for(i = 0; i < ncmds; i++)
	tcvp_event_send(qs, tcvp_event_get(commands[i]));

    if(pl || sel_ui){
	qr = eventq_new(tcref);
	sprintf(qn, "%s/status", qname);
	eventq_attach(qr, qn, EVENTQ_RECV);
	pthread_create(&evt_thr, NULL, tcl_event, NULL);

	sem_init(&psm, 0, 0);
	sa.sa_handler = sigint;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	intr = 1;
	pthread_create(&intr_thr, NULL, tcl_intr, NULL);
    } else {
	tc2_request(TC2_UNLOAD_ALL, 0);
    }

    free(qname);
    return 0;
}

extern int
tcl_stop(void)
{
    int i;

    if(validate)
	pthread_join(check_thr, NULL);

    if(pl)
	pl->free(pl);

    if(pll)
	pll->free(pll);

    if(qr){
	tcvp_event_send(qr, -1);
	pthread_join(evt_thr, NULL);
	eventq_delete(qr);
    }

    if(intr){
	intr = 0;
	sem_post(&psm);
	pthread_join(intr_thr, NULL);
	signal(SIGINT, SIG_DFL);
	sem_destroy(&psm);
    }

    if(qs)
	eventq_delete(qs);

    if(addons){
	int i;
	for(i = 0; i < nadd; i++)
	    if(addons[i])
		tcfree(addons[i]);
	free(addons);
	free(aonames);
    }

    if(commands)
	free(commands);

    for(i = 0; i < nfiles; i++)
	free(files[i]);
    for(i = 0; i < npl; i++)
	free(playlist[i]);

    return 0;
}

/* Identifiers for long-only options */
#define OPT_TC2_DEBUG 128
#define OPT_TC2_VERBOSE 129
#define OPT_TRACE_MALLOC 130
#define OPT_ASPECT 131
#define OPT_SKIN 132
#define OPT_NEXT 133
#define OPT_PREV 134
#define OPT_PLAY 135
#define OPT_PAUSE 136
#define OPT_STOP 137

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
	{"noshuffle", no_argument, 0, 'Z'},
	{"playlist", required_argument, 0, '@'},
	{"fullscreen", required_argument, 0, 'f'},
	{"aspect", required_argument, 0, OPT_ASPECT},
	{"output", required_argument, 0, 'o'},
	{"profile", required_argument, 0, 'P'},
	{"skin", required_argument, 0, OPT_SKIN},
	{"parallel", no_argument, 0, 'p'},
	{"addon", required_argument, 0, 'x'},
	{"noaddon", required_argument, 0, 'X'},
	{"next", no_argument, 0, OPT_NEXT},
	{"prev", no_argument, 0, OPT_PREV},
	{"play", no_argument, 0, OPT_PLAY},
	{"pause", no_argument, 0, OPT_PAUSE},
	{"stop", no_argument, 0, OPT_STOP},
	{"daemon", no_argument, 0, 'D'},
	{"trace-malloc", no_argument, 0, OPT_TRACE_MALLOC},
	{0, 0, 0, 0}
    };

    for(;;){
	int c, option_index = 0, s;
	char *ot;
     
	c = getopt_long(argc, argv, "hA:a:V:v:Cs:u:zZ@:fo:P:t:px:X:D",
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

	case 'Z':
	    shuffle = -1;
	    break;

	case '@':
	    playlist = realloc(playlist, (npl+1) * sizeof(*playlist));
	    playlist[npl++] = optarg;
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

	case OPT_SKIN:
	    skin = optarg;
	    break;

	case 'p':
	    prl = 1;
	    break;

	case 'x':
	    add_addon(optarg);
	    break;

	case 'X':
	    nanames = realloc(nanames, (nnadd + 1) * sizeof(*nanames));
	    nanames[nnadd++] = optarg;
	    break;

	case OPT_NEXT:
	    add_cmd("TCVP_PL_NEXT");
	    break;

	case OPT_PREV:
	    add_cmd("TCVP_PL_PREV");
	    break;

	case OPT_PLAY:
	    add_cmd("TCVP_PL_START");
	    break;

	case OPT_PAUSE:
	    add_cmd("TCVP_PAUSE");
	    break;

	case OPT_STOP:
	    add_cmd("TCVP_PL_STOP");
	    add_cmd("TCVP_CLOSE");
	    break;

	case 'D':
	    isdaemon = 1;
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

    if(isdaemon)
	daemon(0, 0);

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
    if(playlist)
	free(playlist);

    return 0;
}

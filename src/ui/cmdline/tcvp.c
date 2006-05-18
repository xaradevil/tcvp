/**
    Copyright (C) 2003-2004  Michael Ahlberg, Måns Rullgård

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
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
#include <fnmatch.h>
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
static eventq_t qr, qs;
static int have_ui, have_local;
static uint32_t pl_sflags, pl_cflags;
static int prl;
static char **modnames, **nmodnames;
static int nadd, nnadd;
static tcvp_module_t **modules;
static char **commands;
static int ncmds;
static int isdaemon;
static int clr_pl;

#define have_cmds (ncmds || clr_pl)

typedef union tcvp_cl_event {
    int type;
    tcvp_state_event_t state;
    tcvp_pl_state_event_t plstate;
    tcvp_load_event_t load;
} tcvp_cl_event_t;

static void
show_help(void)
{
    /* FIXME: better helpscreen */
    printf("TCVP " VERSION "\n\n"
	   "usage: tcvp [options] [files]\n\n"
	   "options:\n"
	   "   -@ file --playlist=file       Load playlist from file\n"
	   "   -a #    --audio-stream=#      Select audio stream\n"
	   "   -A dev  --audio-device=dev    Select audio device\n"
	   "   -C      --validate            Check file integrity\n"
	   "   -D      --daemon              Fork into background\n"
	   "   -f      --fullscreen          Fill entire screen\n"
	   "   -h      --help                This text\n"
	   "   -o file --output=file         Set output file\n"
	   "   -P name --profile=name        Select profile\n"
	   "   -s t    --seek=t              Seek t seconds at start\n"
	   "   -S #    --subtitle=#          Select subtitle stream\n"
	   "   -t t    --time=t              Play t seconds\n"
	   "   -u name --user-interface=name Select user interface\n"
	   "   -v #    --video-stream=#      Select video stream\n"
	   "   -V dev  --video-device=dev    Select video device\n"
	   "   -x name --module=name         Enable module\n"
	   "   -X name --nomodule=name       Disable module\n"
	   "   -z      --shuffle             Enable shuffle\n"
	   "   -Z      --noshuffle           Disable shuffle\n"
	   "           --aspect=a[/b]        Force video aspect ratio\n"
	   "           --clear               Clear playlist\n"
	   "           --next\n"
	   "           --pause\n"
	   "           --play\n"
	   "           --prev\n"
	   "           --skin=file           Select skin\n"
	   "           --stop\n"
	   "           --tc2-print=tag,level Set print level for tag\n");
}

static void
add_module(char *pfx, char *ao)
{
    char *mod = malloc(strlen(pfx) + strlen(ao) + 2);
    int i;

    sprintf(mod, "%s%s", pfx, ao);

    for(i = 0; i < nadd; i++){
	if(!strcmp(modnames[i], mod)){
	    free(mod);
	    return;
	}
    }

    modnames = realloc(modnames, (nadd + 1) * sizeof(*modnames));
    modnames[nadd++] = mod;
}

static void
add_nomodule(char *nm)
{
    nmodnames = realloc(nmodnames, (nnadd + 1) * sizeof(*nmodnames));
    nmodnames[nnadd++] = nm;
}

static int
nomodule(char *ao)
{
    int i;
    for(i = 0; i < nnadd; i++)
	if(!fnmatch(nmodnames[i], ao, 0))
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
		tc2_print("TCVP", TC2_PRINT_ERROR, "Error opening file.\n");
		break;
	    case TCVP_STATE_END:
		if(!prl)
		    break;
	    }
	} else if(te->type == TCVP_PL_STATE){
	    if(te->plstate.state == TCVP_PL_STATE_END){
		if(!isdaemon && !have_ui)
		    tc2_request(TC2_UNLOAD_ALL, 0);
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
static pthread_t sig_thr;

static void
sigint(int s)
{
    if(pthread_self() == sig_thr){
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
	    if(sig == SIGINT && !prl && !have_ui){
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

    tc2_request(TC2_UNLOAD_ALL, 0);
    return NULL;
}

extern int
tcl_init(char *p)
{
    struct sigaction sa;
    tcconf_section_t *tcvp_conf, *prconf = NULL, *localf;
    char *profile = NULL;
    int i;

    if(validate){
	pthread_create(&check_thr, NULL, tcl_check, NULL);
	return 0;
    }

    tcvp_conf = tc2_get_conf("TCVP");
    if(!tcvp_conf){
	tc2_print("TCVP", TC2_PRINT_ERROR,
		  "configuration file missing or corrupt\n");
	tc2_request(TC2_UNLOAD_ALL, 0);
	return 0;
    }

    if(tcconf_getvalue(cf, "profile", "%s", &profile) <= 0)
	tcconf_getvalue(tcvp_conf, "default_profile", "%s", &profile);
    if(profile){
	char pr[strlen(profile) + 12];
	sprintf(pr, "profiles/%s", profile);
	if((prconf = tcconf_getsection(tcvp_conf, pr))){
	    void *nv = NULL;
	    char *aon;
	    while(tcconf_nextvalue(prconf, "module", &nv, "%s", &aon) > 0){
		add_module("", aon);
		free(aon);
	    }
            nv = NULL;
            while(tcconf_nextvalue(prconf, "nomodule", &nv, "%s", &aon) > 0){
                add_nomodule(aon);
            }
	    tcfree(prconf);
	}
	free(profile);
    }
    tcfree(tcvp_conf);

    for(i = 0; i < tcvp_ui_cmdline_conf_module_count; i++)
	add_module("", tcvp_ui_cmdline_conf_module[i]);

    modules = calloc(nadd, sizeof(*modules));
    for(i = 0; i < nadd; i++){
	tcvp_module_new_t anf;

	if(nomodule(modnames[i]))
	    continue;

	anf = tc2_get_symbol(modnames[i], "new");
	if(anf){
	    modules[i] = anf(cf);
	}
    }

    for(i = 0; i < nadd; i++){
	if(modules[i]){
	    if(modules[i]->init(modules[i])){
		tcfree(modules[i]);
		modules[i] = NULL;
	    }
	}
    }

    have_ui = !tcconf_getvalue(cf, "features/local/ui", "");
    localf = tcconf_getsection(cf, "features/local");
    if(localf){
	have_local = 1;
	tcfree(localf);
    }

    if(!have_cmds && !nfiles && !npl && !have_ui && !isdaemon &&
       !pl_sflags && !pl_cflags){
	show_help();
	tc2_request(TC2_UNLOAD_ALL, 0);
	return 0;
    }

    qs = tcvp_event_get_sendq(cf, "control");

    if(clr_pl){
	tcvp_event_send(qs, TCVP_PL_STOP);
	tcvp_event_send(qs, TCVP_CLOSE);
	tcvp_event_send(qs, TCVP_PL_REMOVE, 0, -1);
    }

    if(!prl){
	if(nfiles)
	    tcvp_event_send(qs, TCVP_PL_ADD, files, nfiles, -1);
	for(i = 0; i < npl; i++)
	    tcvp_event_send(qs, TCVP_PL_ADDLIST, playlist[i], -1);
    } else {
	tcvp_event_send(qs, TCVP_OPEN_MULTI, nfiles, files);
    }

    pl_sflags &= ~pl_cflags;

    if(pl_sflags)
	tcvp_event_send(qs, TCVP_PL_FLAGS, pl_sflags, TCVP_PL_FLAGS_OR);
    if(pl_cflags)
	tcvp_event_send(qs, TCVP_PL_FLAGS, ~pl_cflags, TCVP_PL_FLAGS_AND);

    if(!have_cmds && tcvp_ui_cmdline_conf_autoplay){
	if(prl){
	    tcvp_event_send(qs, TCVP_START);
	} else {
	    tcvp_event_send(qs, TCVP_PL_START);
	}
    }

    for(i = 0; i < ncmds; i++)
	tcvp_event_send(qs, tcvp_event_get(commands[i]));

    if(!have_cmds && have_local){
	qr = tcvp_event_get_recvq(cf, "status", NULL);
	pthread_create(&evt_thr, NULL, tcl_event, NULL);

	tcvp_event_send(qs, TCVP_QUERY);

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

    return 0;
}

extern int
tcl_stop(void)
{
    int i;

    tc2_print("CMDLINE", TC2_PRINT_DEBUG, "tcl_stop\n");

    if(validate)
	pthread_join(check_thr, NULL);

    if(qr){
	tcvp_event_send(qr, -1);
	pthread_join(evt_thr, NULL);
	eventq_delete(qr);
    }

    if(intr){
	tc2_print("CMDLINE", TC2_PRINT_DEBUG, "stopping signal handler\n");
	intr = 0;
	sem_post(&psm);
	pthread_join(intr_thr, NULL);
	signal(SIGINT, SIG_DFL);
	sem_destroy(&psm);
    }

    if(qs)
	eventq_delete(qs);

    if(modules){
	tc2_print("CMDLINE", TC2_PRINT_DEBUG, "unloading modules\n");

	for(i = 0; i < nadd; i++){
	    tc2_print("CMDLINE", TC2_PRINT_DEBUG, "  %s\n", modnames[i]);
	    if(modules[i])
		tcfree(modules[i]);
	    if(modnames[i])
		free(modnames[i]);
	}
	free(modules);
	free(modnames);
    }

    if(commands)
	free(commands);

    for(i = 0; i < nfiles; i++)
	free(files[i]);
    for(i = 0; i < npl; i++)
	free(playlist[i]);

    tc2_print("CMDLINE", TC2_PRINT_DEBUG, "tcl_stop done\n");

    return 0;
}

static void
set_number_or_string(tcconf_section_t *cf, char *cn, char *v)
{
    char *t;
    long n;

    n = strtol(v, &t, 0);
    if(*t)
	tcconf_setvalue(cf, cn, "%i%s", -1, v);
    else
	tcconf_setvalue(cf, cn, "%i", n);
}

/* Identifiers for long-only options */
#define OPT_TC2_PRINT 128
#define OPT_TRACE_MALLOC 130
#define OPT_ASPECT 131
#define OPT_SKIN 132
#define OPT_NEXT 133
#define OPT_PREV 134
#define OPT_PLAY 135
#define OPT_PAUSE 136
#define OPT_STOP 137
#define OPT_CLEAR 138
#define OPT_ATTR 139
#define OPT_ROOT 140
#define OPT_WINDOW 141
#define OPT_PORT 142

static int
parse_options(int argc, char **argv)
{
    struct option long_options[] = {
	{"help", no_argument, 0, 'h'},
	{"audio-device", required_argument, 0, 'A'},
	{"audio-stream", required_argument, 0, 'a'},
	{"video-device", required_argument, 0, 'V'},
	{"video-stream", required_argument, 0, 'v'},
	{"subtitle", required_argument, 0, 'S'},
	{"validate", no_argument, 0, 'C'},
	{"seek", required_argument, 0, 's'},
	{"time", required_argument, 0, 't'},
	{"user-interface", required_argument, 0, 'u'},
	{"tc2-print", required_argument, 0, OPT_TC2_PRINT},
	{"shuffle", no_argument, 0, 'z'},
	{"noshuffle", no_argument, 0, 'Z'},
	{"repeat", no_argument, 0, 'r'},
	{"norepeat", no_argument, 0, 'R'},
	{"playlist", required_argument, 0, '@'},
	{"fullscreen", no_argument, 0, 'f'},
	{"root", no_argument, 0, OPT_ROOT },
	{"window", required_argument, 0, OPT_WINDOW },
	{"aspect", required_argument, 0, OPT_ASPECT},
	{"output", required_argument, 0, 'o'},
	{"profile", required_argument, 0, 'P'},
	{"skin", required_argument, 0, OPT_SKIN},
	{"parallel", no_argument, 0, 'p'},
	{"module", required_argument, 0, 'x'},
	{"nomodule", required_argument, 0, 'X'},
	{"next", no_argument, 0, OPT_NEXT},
	{"prev", no_argument, 0, OPT_PREV},
	{"play", no_argument, 0, OPT_PLAY},
	{"pause", no_argument, 0, OPT_PAUSE},
	{"stop", no_argument, 0, OPT_STOP},
	{"clear", no_argument, 0, OPT_CLEAR},
	{"daemon", no_argument, 0, 'D'},
	{"title", required_argument, 0, OPT_ATTR},
	{"artist", required_argument, 0, OPT_ATTR},
	{"album", required_argument, 0, OPT_ATTR},
	{"port", required_argument, 0, OPT_PORT},
	{0, 0, 0, 0}
    };

    for(;;){
	int c, opt_index = 0, s;
	char *ot;
     
	c = getopt_long(argc, argv, "hA:a:V:v:Cs:u:zZ@:fo:P:t:px:X:DrRS:",
			long_options, &opt_index);
	
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
	    set_number_or_string(cf, "audio/stream", optarg);
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

	case 'S':
	    set_number_or_string(cf, "subtitle/stream", optarg);
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
	    if(strcmp(optarg, "none")){
		add_module("tcvp/ui/", optarg);
		tcconf_setvalue(cf, "force_ui", "%i", 1);
	    } else {
		add_nomodule("tcvp/ui/*");
	    }
	    break;

	case 'z':
	    pl_sflags |= TCVP_PL_FLAG_SHUFFLE;
	    break;

	case 'Z':
	    pl_cflags |= TCVP_PL_FLAG_SHUFFLE;
	    break;

	case 'r':
	    pl_sflags |= TCVP_PL_FLAG_REPEAT;
	    break;

	case 'R':
	    pl_cflags |= TCVP_PL_FLAG_REPEAT;
	    break;

	case '@':
	    playlist = realloc(playlist, (npl+1) * sizeof(*playlist));
	    playlist[npl++] = optarg;
	    break;

	case 'f':
	    tcconf_setvalue(cf, "video/fullscreen", "%i", 1);
	    break;

	case OPT_ROOT:
            optarg = "root";
        case OPT_WINDOW:
            tcconf_setvalue(cf, "video/window", "%s", optarg);
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
	    ot = fullpath(optarg);
	    tcconf_setvalue(cf, "outname", "%s", ot);
	    free(ot);
	    break;

	case 'P':
	    tcconf_setvalue(cf, "profile", "%s", optarg);
	    break;

	case OPT_SKIN:
	    tcconf_setvalue(cf, "skin", "%s", optarg);
	    break;

	case 'p':
	    prl = 1;
	    break;

	case 'x':
	    add_module("tcvp/", optarg);
	    break;

	case 'X':
	    add_nomodule(strdup(optarg));
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

	case OPT_CLEAR:
	    clr_pl = 1;
	    break;

	case 'D':
	    isdaemon = 1;
	    break;

	case OPT_ATTR:
	    tcconf_setvalue(cf, "attr", "%s%s",
			    long_options[opt_index].name, optarg);
	    break;

	case OPT_TC2_PRINT:
	    ot = strchr(optarg, ',');
	    if(!ot)
		break;
	    *ot++ = 0;
	    s = strtol(ot, NULL, 0);
	    tc2_setprint(optarg, 0, s, "default");
	    break;

	case OPT_PORT:
	    tcconf_setvalue(cf, "remote/port", "%i", strtol(optarg, NULL, 0));
	    break;
	}
    }

    return optind;
}

extern int
main(int argc, char **argv)
{
    int opt_num, i;
    char userconf[1024];

    snprintf(userconf, 1024, "%s/.tcvp/tcvp.conf", getenv("HOME"));
    tc2_add_config(TCVP_CONF);
    tc2_add_config(userconf);
    tc2_init();

    cf = tcconf_new(NULL);

    opt_num = parse_options(argc, argv);
    nfiles = argc - opt_num;
    files = argv + opt_num;

    for(i = 0; i < nfiles; i++)
	files[i] = fullpath(files[i]);
    for(i = 0; i < npl; i++)
	playlist[i] = fullpath(playlist[i]);

    if(isdaemon)
	daemon(0, 0);
    sig_thr = pthread_self();

    tc2_request(TC2_ADD_MODULE, 0, NULL, &MODULE_INFO);
    tc2_request(TC2_LOAD_MODULE, 0, MODULE_INFO.name, NULL);
    tc2_run();
    tc2_free();

    tcfree(cf);
    if(playlist)
	free(playlist);

    for(i = 0; i < nnadd; i++)
        free(nmodnames[i]);
    free(nmodnames);

    return 0;
}

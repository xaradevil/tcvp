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
#include <tclist.h>
#include <tcalloc.h>
#include <tcvpx_tc2.h>
#include <string.h>
#include <unistd.h>

#include "tcvpx.h"
#include "tcvpctl.h"


int quit = 0;

player_t *pl;
eventq_t qs;
eventq_t qr;

int s_time;
int s_length;
static int show_time = TCTIME_ELAPSED;

int p_state = STOPPED;

static pthread_t xth, eth, sth;

list *files;
list_item *flist_curr;
char *current_file;


extern int
toggle_time(tcwidget_t *w, void *p)
{
    if(show_time == TCTIME_ELAPSED) {
	show_time = TCTIME_REMAINING;
    } else if(show_time == TCTIME_REMAINING) {
	show_time = TCTIME_ELAPSED;
    }

    return 0;
}


extern int
update_time(skin_t *skin)
{
    char text[7];
    int t = 0;

    if(show_time == TCTIME_ELAPSED) {
	t = s_time;
    } else if(show_time == TCTIME_REMAINING) {
	if(s_length > 0){
	    t = s_length - s_time;
	} else {
	    t = 0;
	}
    }

    if(s_length > 0){
	change_seek_bar(skin->seek_bar, (double)t/s_length);
    }

    snprintf(text, 7, "%3d:%02d", t/60, t%60);
    change_label(skin->time, text);

    XSync(xd, False);
    
    return 0;
}


extern int
tcvpx_init(char *p)
{
    char *qname, *qn;
    skin_t *skin;

    files = list_new(TC_LOCK_NONE);
    flist_curr = NULL;

    if(p){
	char *f;
	qname = p;
	
	f = qname + strlen(qname) + 1;
	while(strlen(f)>0) {
	    list_push(files, f);
	    f += strlen(f)+1;
	}
	current_file = list_next(files, &flist_curr);
	
    } else {
	conf_section *cs = conf_new(NULL);
	pl = tcvp_new(cs);
	conf_getvalue(cs, "qname", "%s", &qname);
    }

    qs = eventq_new(NULL);
    qn = alloca(strlen(qname) + 10);
    sprintf(qn, "%s/control", qname);
    eventq_attach(qs, qn, EVENTQ_SEND);

    qr = eventq_new(tcref);
    sprintf(qn, "%s/status", qname);
    eventq_attach(qr, qn, EVENTQ_RECV);
    sprintf(qn, "%s/timer", qname);
    eventq_attach(qr, qn, EVENTQ_RECV);

    skin=load_skin(tcvp_ui_tcvpx_conf_skin);

    create_window(skin);

    create_ui(skin);
    update_root(skin);

    XMapWindow (xd, xw);
    XMapSubwindows(xd, xw);

    update_time(skin);
    
    repaint_widgets();
    draw_widgets();

    XSync(xd, False);

    pthread_create(&xth, NULL, x11_event, skin);
    pthread_create(&eth, NULL, tcvp_event, skin);
    pthread_create(&sth, NULL, scroll_labels, NULL);

    return 0;
}


extern int
tcvpx_shdn(void)
{
    tcvp_stop(NULL, NULL);

    XDestroyWindow(xd, xw);
    XSync(xd, False);

    quit = 1;

    pthread_join(sth, NULL);
    /* FIXME: join event thread */
/*     pthread_join(eth, NULL); */
    pthread_join(xth, NULL);

    XCloseDisplay(xd);

    eventq_delete(qs);

    if(pl)
	pl->free(pl);

    return 0;
}

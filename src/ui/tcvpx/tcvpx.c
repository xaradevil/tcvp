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

static pthread_t xth, eth, sth;

extern int
tcvpx_init(char *p)
{
    char *qname, *qn;
    skin_t *skin;

    if(p){
        qname = p;
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

    init_dynamic();
    init_graphics();

    if((skin=load_skin(tcvp_ui_tcvpx_conf_skin)) == NULL){
	fprintf(stderr, "Unable to load skin: \"%s\"\n",
		tcvp_ui_tcvpx_conf_skin);
	return -1;
    }

    create_window(skin);

    if(create_ui(skin) != 0){
	fprintf(stderr, "Unable to load skin: \"%s\"\n",
		tcvp_ui_tcvpx_conf_skin);
	return -1;
    }

    update_root(skin);

    XMapWindow (xd, skin->xw);
    XMapSubwindows(xd, skin->xw);

    if(tcvp_ui_tcvpx_conf_sticky != 0) {
	wm_set_sticky(skin, 1);
    }

    if(tcvp_ui_tcvpx_conf_always_on_top != 0) {
	wm_set_always_on_top(skin, 1);
    }

    repaint_widgets();
    draw_widgets();
    
    XSync(xd, False);

    pthread_create(&xth, NULL, x11_event, NULL);
    pthread_create(&eth, NULL, tcvp_event, NULL);
    if(list_items(sl_list)>0) {
	pthread_create(&sth, NULL, scroll_labels, NULL);
    }
    return 0;
}


extern int
tcvpx_shdn(void)
{
    tcvp_stop(NULL, NULL);

    if(list_items(sl_list)>0) {
	pthread_join(sth, NULL);
    }
    /* FIXME: join event thread */
/*     pthread_join(eth, NULL); */
    pthread_join(xth, NULL);

    XCloseDisplay(xd);

    eventq_delete(qs);

    if(pl)
	pl->free(pl);

    return 0;
}

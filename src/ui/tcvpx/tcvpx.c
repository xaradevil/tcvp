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

#define xpos tcvp_ui_tcvpx_conf_xposition
#define ypos tcvp_ui_tcvpx_conf_yposition

player_t *pl;
eventq_t qs;
eventq_t qr;

static pthread_t eth;

extern int
tcvpx_init(char *p)
{
    char *qname, *qn;
    skin_t *skin;

    if(p){
        qname = strdup(p);
    } else {
	tcconf_section_t *cs = tcconf_new(NULL);
	pl = tcvp_new(cs);
	tcconf_getvalue(cs, "qname", "%s", &qname);
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
    free(qname);

    init_dynamic();
    init_skins();
    init_events();

    xtk_init_graphics();
    xtk_set_dnd_cb(tcvp_add_file);

    if((skin = load_skin(tcvp_ui_tcvpx_conf_skin)) == NULL){
	fprintf(stderr, "Unable to load skin: \"%s\"\n",
		tcvp_ui_tcvpx_conf_skin);
	return -1;
    }

    skin->window = xtk_create_window("TCVP", skin->width, skin->height);

    if(create_ui(skin->window, skin, skin->config, NULL) != 0){
	fprintf(stderr, "Unable to load skin: \"%s\"\n",
		tcvp_ui_tcvpx_conf_skin);
	return -1;
    }

    xtk_update_root(skin->window);

    xtk_show_window(skin->window);

    if(xpos > -2 && ypos > -2) {
	xtk_position_t pos;
	xtk_size_t *ss = xtk_get_screen_size();

	if(xpos < 0) {
	    xpos = ss->w - skin->width;
	}
	if(ypos < 0) {
	    ypos = ss->h - skin->height;
	}

	pos.x = xpos;
	pos.y = ypos;

	xtk_set_window_position(skin->window, &pos);

	free(ss);
    }

    if(tcvp_ui_tcvpx_conf_sticky != 0) {
	xtk_set_sticky(skin->window, 1);
	skin->state |= ST_STICKY;
    }

    if(tcvp_ui_tcvpx_conf_always_on_top != 0) {
	xtk_set_always_on_top(skin->window, 1);
	skin->state |= ST_ON_TOP;
    }

    update_time();

    xtk_repaint_widgets();
    xtk_draw_widgets();

    pthread_create(&eth, NULL, tcvp_event, NULL);
    return 0;
}


extern int
tcvpx_shdn(void)
{
    tcvp_stop(NULL, NULL);

    tcvp_event_send(qr, -1);

    xtk_shutdown_graphics();

    pthread_join(eth, NULL);

    cleanup_skins();
    free_dynamic();

    eventq_delete(qs);
    eventq_delete(qr);

    if(pl)
	pl->free(pl);

    return 0;
}

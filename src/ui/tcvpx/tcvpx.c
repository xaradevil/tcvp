/**
    Copyright (C) 2003, 2004  Michael Ahlberg, M�ns Rullg�rd

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
#define rwidth (*xtk_display_width)
#define rheight (*xtk_display_height)

eventq_t qs;
eventq_t qr;

static pthread_t eth;

extern int
tcvpx_init(char *p)
{
    char *qname = NULL, *qn, *skinfile = tcvp_ui_tcvpx_conf_skin;
    skin_t *skin;
    tcconf_section_t *cs = tc2_get_conf(MODULE_INFO.name);

    if(!cs)
	cs = tcconf_new(NULL);
    else
	tcconf_getvalue(cs, "qname", "%s", &qname);

    tcconf_getvalue(cs, "skin", "%s", &skinfile);

    tcfree(cs);

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

    if((skin = load_skin(skinfile)) == NULL){
	fprintf(stderr, "Unable to load skin: \"%s\"\n", skinfile);
	return -1;
    }

    skin->window = xtk_window_create(NULL, 0, 0, skin->width, skin->height);
    xtk_window_set_dnd_callback(skin->window, tcvp_add_file);
    xtk_window_set_title(skin->window, "TCVP");

    if(create_ui(skin->window, skin, skin->config, NULL) != 0){
	fprintf(stderr, "Unable to load skin: \"%s\"\n", skinfile);
	return -1;
    }

    xtk_window_show(skin->window);

    if(xpos > -2 && ypos > -2) {
	xtk_position_t pos;

	if(xpos < 0) {
	    xpos = rwidth - skin->width;
	}
	if(ypos < 0) {
	    ypos = rheight - skin->height;
	}

	pos.x = xpos;
	pos.y = ypos;
	xtk_window_set_position(skin->window, &pos);
    }

    if(tcvp_ui_tcvpx_conf_sticky != 0) {
	xtk_window_set_sticky(skin->window, 1);
	skin->state |= ST_STICKY;
    }

    if(tcvp_ui_tcvpx_conf_always_on_top != 0) {
	xtk_window_set_always_on_top(skin->window, 1);
	skin->state |= ST_ON_TOP;
    }

    update_time();

    pthread_create(&eth, NULL, tcvp_event, NULL);
    xtk_run();

/*     tc2_request(TC2_LOAD_MODULE, 1, "Shell", NULL); */

    return 0;
}


extern int
tcvpx_shdn(void)
{
    tcvp_stop(NULL, NULL);

    tcvp_event_send(qr, -1);

    xtk_shutdown();
    
    pthread_join(eth, NULL);

    cleanup_skins();
    free_dynamic();

    eventq_delete(qs);
    eventq_delete(qr);

    return 0;
}

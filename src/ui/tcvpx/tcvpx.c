/**
    Copyright (C) 2003, 2004  Michael Ahlberg, Måns Rullgård

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

extern int
tcvpx_init(tcvp_module_t *tm)
{
    tcvpx_t *tx = tm->private;
    char *qname = NULL, *qn, *skinfile = tcvp_ui_tcvpx_conf_skin;
    skin_t *skin;
    widget_data_t *wd;

    if(!tcconf_getvalue(tx->conf, "features/ui", "") &&
       tcconf_getvalue(tx->conf, "force_ui", ""))
	return -1;

    qname = tcvp_event_get_qname(tx->conf);
    tcconf_getvalue(tx->conf, "skin", "%s", &skinfile);

    qs = eventq_new(NULL);
    qn = alloca(strlen(qname) + 10);
    sprintf(qn, "%s/control", qname);
    eventq_attach(qs, qn, EVENTQ_SEND);

    free(qname);

    init_dynamic();
    init_skins();

    if((skin = load_skin(skinfile)) == NULL){
	tc2_print("TCVPX", TC2_PRINT_ERROR,
		  "Unable to load skin: \"%s\"\n", skinfile);
	return -1;
    }

    skin->window = xtk_window_create(NULL, 0, 0, skin->width, skin->height);
    xtk_window_set_dnd_callback(skin->window, tcvp_add_file);
    xtk_window_set_class(skin->window, "TCVP");

    wd = calloc(sizeof(*wd), 1);
    xtk_widget_container_set_data(skin->window, wd);

    if(tcvp_ui_tcvpx_conf_change_window_title) {
	char *default_text = malloc(1024);
	wd->value = tcvp_ui_tcvpx_conf_window_title;
	register_textwidget(skin->window, wd->value);

	parse_text(wd->value, default_text, 1024);
	xtk_window_set_title(skin->window, default_text);
	free(default_text);
    } else {
	xtk_window_set_title(skin->window, "TCVP");
    }

    if(create_ui(skin->window, skin, skin->config, NULL) != 0){
	tc2_print("TCVPX", TC2_PRINT_ERROR,
		  "Unable to load skin: \"%s\"\n", skinfile);
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

    xtk_run();

    tcconf_setvalue(tx->conf, "features/ui", "");
    tcconf_setvalue(tx->conf, "features/local/ui", "");

/*     tc2_request(TC2_LOAD_MODULE, 1, "Shell", NULL); */

    return 0;
}

extern void
tcvpx_free(void *p)
{
    tcvpx_t *tx = p;

    xtk_shutdown();
    
    cleanup_skins();
    free_dynamic();

    if(qs)
	eventq_delete(qs);
    tcfree(tx->conf);
}

extern int
tcvpx_new(tcvp_module_t *m, tcconf_section_t *c)
{
    tcvpx_t *tx = tcallocd(sizeof(*tx), NULL, tcvpx_free);
    tx->conf = tcref(c);
    m->private = tx;
    return 0;
}

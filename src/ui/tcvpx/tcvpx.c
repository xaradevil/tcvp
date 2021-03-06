/**
    Copyright (C) 2003 - 2005  Michael Ahlberg, Måns Rullgård

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
    char *skinfile = NULL;
    skin_t *skin;
    widget_data_t *wd;

    tcconf_getvalue(tx->conf, "skin", "%s", &skinfile);

    if(skinfile == NULL) {
        skinfile = strdup(tcvp_ui_tcvpx_conf_skin);
    }

    qs = tcvp_event_get_sendq(tx->conf, "control");

    init_dynamic(tx->conf);
    init_skins();

    if((skin = load_skin(skinfile)) == NULL){
        tc2_print("TCVPX", TC2_PRINT_ERROR,
                  "Unable to load skin: \"%s\"\n", skinfile);
        return -1;
    }

    skin->skin_hash = tchash_new(10, 0, 0);
    tcconf_getvalue(skin->config, "id", "%s", &skin->id);
    if(skin->id != NULL) {
        tchash_search(skin->skin_hash, skin->id, -1, skin, NULL);
    }

    skin->window = xtk_window_create(NULL, 0, 0, skin->width, skin->height);
    xtk_window_set_dnd_callback(skin->window, tcvp_add_file);
    xtk_window_set_class(skin->window, "TCVP");

    if(create_ui(skin->window, skin, skin->config, NULL) != 0){
        tc2_print("TCVPX", TC2_PRINT_ERROR,
                  "Unable to load skin: \"%s\"\n", skinfile);
        return -1;
    }

    wd = tcallocdz(sizeof(*wd), NULL, widgetdata_free);
    wd->action = skin->dblclick;
    wd->skin = tcref(skin);
    xtk_widget_container_set_data(skin->window, wd);

    xtk_window_set_doubleclick_callback(skin->window, lookup_action);

    xtk_window_set_sticky_callback(skin->window, sticky_cb);
    xtk_window_set_on_top_callback(skin->window, on_top_cb);

    char *default_text = malloc(1024);
    tcconf_getvalue(skin->config, "title", "%s", &wd->value);
    if(wd->value == NULL) {
        wd->value = strdup(tcvp_ui_tcvpx_conf_window_title);
    }
    register_textwidget(skin->window, wd->value);

    parse_text(wd->value, default_text, 1024);
    xtk_window_set_title(skin->window, default_text);
    free(default_text);

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

    free(skinfile);

    return 0;
}

extern void
tcvpx_free(void *p)
{
    tcvpx_t *tx = p;

    xtk_shutdown();

    cleanup_skins();
    free_dynamic();
    free_ctl();

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

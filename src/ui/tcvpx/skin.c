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

#include <tcconf.h>
#include "tcvpx.h"
#include <string.h>
#include "tcvpctl.h"
#include <X11/Xlib.h>
#include "widgets.h"

static int tcvp_open_ui(tcwidget_t *w, void *p);
static int tcvp_close_ui(tcwidget_t *w, void *p);
static int tcvp_replace_ui(tcwidget_t *w, void *p);

int ui_count = 1;
hash_table *action_hash;


extern int
register_actions()
{
    typedef struct {
	char *name;
	action_cb_t action;
    } saction_t;

    saction_t actions[] = {
	{"play", tcvp_play},
	{"stop", tcvp_stop},
	{"pause", tcvp_pause},
	{"previous", tcvp_previous},
	{"next", tcvp_next},
	{"open_ui", tcvp_open_ui},
	{"close_ui", tcvp_close_ui},
	{"replace_ui", tcvp_replace_ui},
	{"toggle_time", toggle_time},
	{NULL, NULL}
    };
    int i;

    action_hash = hash_new(10, 0);

    for(i=0; actions[i].name != NULL; i++) {
	hash_search(action_hash, actions[i].name, actions[i].action, NULL);
    }

    return 0;
}


extern int
lookup_action(tcwidget_t *w, void *p)
{
    action_cb_t acb = NULL;
    action_data_t *ad = (action_data_t*)w->common.data;

    if(ad->action != NULL) {
	hash_find(action_hash, ad->action, &acb);
	if(acb) {
	    acb(w, p);
	}
    }

    return 0;
}

extern skin_t*
load_skin(char *skinconf)
{
    char *tmp;
    skin_t *skin = calloc(sizeof(skin_t), 1);
    int i=0;

    if(!(skin->config = conf_load_file (NULL, skinconf))){
	fprintf(stderr, "Error loading file \"%s\".\n", skinconf);
	return NULL;
    }

    skin->file = skinconf;
    skin->path = strdup(skinconf);
    tmp = strrchr(skin->path, '/');
    if(tmp == NULL) {
	free(skin->path);
	skin->path = strdup("");	
    } else {
	*tmp=0;
    }

/*     if(conf_getvalue(skin->config, "name", "%s", &tmp) == 1) */
/* 	printf("Loaded skin: \"%s\"\n", tmp); */

    i = conf_getvalue(skin->config, "size", "%d %d", &skin->width,
		      &skin->height);
    if(i != 2) {
	return NULL;
    }

    skin->enabled = 1;
    skin->widgets = list_new(TC_LOCK_SLOPPY);

    list_push(skin_list, skin);

    return skin;
}


static tcbackground_t*
create_skinned_background(skin_t *skin, conf_section *sec)
{
    char *file;
    int i=0;

    i += conf_getvalue(sec, "background", "%s", &file);

    if(i != 1){
	return NULL;
    }

    return(create_background(skin, file));
}


static tcimage_button_t*
create_skinned_button(skin_t *skin, conf_section *sec)
{
    char *file, *of = NULL, *df = NULL;
    int x, y;
    int i=0;
    action_data_t *ad = calloc(sizeof(*ad), 1);

    i += conf_getvalue(sec, "action", "%s", &ad->action);
    i += conf_getvalue(sec, "image", "%s", &file);
    i += conf_getvalue(sec, "position", "%d %d", &x, &y);

    if(i != 4){
	return NULL;
    }

    conf_getvalue(sec, "mouse_over", "%s", &of);
    conf_getvalue(sec, "pressed", "%s", &df);
    conf_getvalue(sec, "action_data", "%s", &ad->data);

    return create_button(skin, x, y, file, of, df, lookup_action, ad);
}


static int
destroy_skinned_label(tcwidget_t *w)
{
    action_data_t *ad = (action_data_t*)w->common.data;
    unregister_textwidget(w, ad->data);
    return 0;
}

static tclabel_t*
create_skinned_label(skin_t *skin, conf_section *sec)
{
    int x, y;
    int w, h;
    int xoff, yoff;
    char *font;
    char *color;
    int alpha;
    int stype;
    int i=0, j;
    char *action, *text, *default_text;
    action_data_t *ad = calloc(sizeof(*ad), 1);
    tclabel_t *l;

    i += conf_getvalue(sec, "position", "%d %d", &x, &y);
    i += conf_getvalue(sec, "size", "%d %d", &w, &h);
    i += conf_getvalue(sec, "text", "%s", &text);
    i += conf_getvalue(sec, "text_offset", "%d %d", &xoff, &yoff);
    i += conf_getvalue(sec, "font", "%s", &font);
    if((j = conf_getvalue(sec, "color", "%s %d", &color, &alpha))==1){
	alpha = 0xff;
	j++;
    }
    i += j;
    if((j = conf_getvalue(sec, "scroll_style", "%d", &stype))==0){
	stype = 1;
	j++;
    }
    i += j;

    if(i != 11){
	return NULL;
    }

    conf_getvalue(sec, "action", "%s", &action);

    default_text = malloc(1024);
    parse_text(text, default_text);

    ad->action = action;
    ad->data = text;
    l = create_label(skin, x, y, w, h, xoff, yoff, default_text, font,
		     color, alpha, stype, lookup_action, ad);

    register_textwidget((tcwidget_t *)l, text);
    l->ondestroy = destroy_skinned_label;

    return l;
}


static tcseek_bar_t*
create_skinned_seek_bar(skin_t *skin, conf_section *sec, double position,
		       action_cb_t acb)
{
    int x, y;
    int sp_x, sp_y;
    int ep_x, ep_y;
    char *bg, *indicator;
    int i=0;

    i += conf_getvalue(sec, "position", "%d %d", &x, &y);
    i += conf_getvalue(sec, "start_position", "%d %d", &sp_x, &sp_y);
    i += conf_getvalue(sec, "end_position", "%d %d", &ep_x, &ep_y);
    i += conf_getvalue(sec, "background_image", "%s", &bg);
    i += conf_getvalue(sec, "indicator_image", "%s", &indicator);

    if(i != 8){
	return NULL;
    }

    return(create_seek_bar(skin, x, y, sp_x, sp_y, ep_x, ep_y, bg,
			   indicator, position, acb, NULL));
}


static int
destroy_skinned_state(tcwidget_t *w)
{
    action_data_t *ad = (action_data_t*)w->common.data;
    unregister_textwidget(w, ad->data);
    return 0;
}

static tcstate_t*
create_skinned_state(skin_t *skin, conf_section *sec)
{
    int x, y;
    int ns = 0;
    char **imgs = NULL, **states = NULL;
    void *c = NULL;
    int i;
    char *img, *st, def_state[512];
    action_data_t *ad = calloc(sizeof(*ad), 1);

    i = conf_getvalue(sec, "position", "%d %d", &x, &y);
    i += conf_getvalue(sec, "state", "%s", &ad->data);
    if (i != 3) {
	return NULL;
    }

    for(i = conf_nextvalue(sec, "image", &c, "%s %s", &st, &img); c;
	i = conf_nextvalue(sec, "image", &c, "%s %s", &st, &img)){
	if(i == 2) {
	    imgs = realloc(imgs, sizeof(*imgs)*(ns+1));
	    states = realloc(states, sizeof(*states)*(ns+1));
	    imgs[ns] = img;
	    states[ns] = st;
	    ns++;
	}
    }

    conf_getvalue(sec, "action", "%s", &ad->action);

    parse_text(ad->data, def_state);

    if(ns > 0) {
	tcstate_t *s;

	s = create_state(skin, x, y, ns, imgs, states, def_state,
			 lookup_action, ad);
	register_textwidget((tcwidget_t *)s, ad->data);
	s->ondestroy = destroy_skinned_state;

	return s;
    } else {
	return NULL;
    }
}
    

static int
tcvp_replace_ui(tcwidget_t *w, void *p)
{
    if(tcvp_open_ui(w, p) == 0) {
	tcvp_close_ui(w, p);
    }
    return 0;
}

static int
tcvp_close_ui(tcwidget_t *w, void *p)
{
    destroy_window(w->common.skin);

    ui_count--;
    if(ui_count == 0) {
	tcvp_quit();
    }

    return 0;
}

static int
tcvp_open_ui(tcwidget_t *w, void *p)
{
    char *uifile = alloca(strlen(w->common.skin->path) +
			  strlen(((action_data_t *)w->common.data)->data) + 2);

    sprintf(uifile, "%s/%s", w->common.skin->path,
	    (char*)((action_data_t *)w->common.data)->data);

    skin_t *ui = load_skin(uifile);

    if(!ui) {
	return -1;
    }

    create_window(ui);
    create_ui(ui);

    XMapWindow (xd, ui->xw);
    XMapSubwindows(xd, ui->xw);

    if(tcvp_ui_tcvpx_conf_sticky != 0) {
	wm_set_sticky(ui, 1);
    }

    if(tcvp_ui_tcvpx_conf_always_on_top != 0) {
	wm_set_always_on_top(ui, 1);
    }

    ui_count++;

    return 0;
}


extern int
create_ui(skin_t *skin)
{
    conf_section *sec;
    void *w, *s;

    if((skin->background = create_skinned_background(skin, skin->config)) ==
       NULL) {
	/* No background - No good */
	return -1;
    }
    list_push(skin->widgets, skin->background);

    for(s=NULL, sec = conf_nextsection(skin->config, "button", &s);
	sec != NULL; sec = conf_nextsection(skin->config, "button", &s)) {

	w = create_skinned_button(skin, sec);
	if(w){
	    list_push(skin->widgets, w);
	}
    }

    for(s=NULL, sec = conf_nextsection(skin->config, "label", &s);
	sec != NULL; sec = conf_nextsection(skin->config, "label", &s)) {

	w = create_skinned_label(skin, sec);
	if(w){
	    list_push(skin->widgets, w);
	}
    }

    for(s=NULL, sec = conf_nextsection(skin->config, "state", &s);
	sec != NULL; sec = conf_nextsection(skin->config, "state", &s)) {

	w = create_skinned_state(skin, sec);
	if(w){
	    list_push(skin->widgets, w);
	}
    }

    sec = conf_getsection(skin->config, "seek_bar");
    if(sec){
	w = skin->seek_bar = create_skinned_seek_bar(skin, sec, 0, tcvp_seek);
	if(w) {
	    list_push(skin->widgets, w);
	}
    }

    return 0;
}

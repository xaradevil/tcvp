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

static int tcvp_open_ui(xtk_widget_t *w, void *p);
static int tcvp_close_ui(xtk_widget_t *w, void *p);
static int tcvp_replace_ui(xtk_widget_t *w, void *p);

int ui_count = 1;
hash_table *action_hash;

extern image_info_t*
load_image(char *skinpath, char *file)
{
    if(file) {
	FILE *f;


	char fn[1024];
	image_info_t *img;

	snprintf(fn, 1023, "%s/%s", skinpath, file);
	f = fopen(fn,"r");

	img = malloc(sizeof(image_info_t));

	img->flags = IMAGE_COLOR_TYPE | IMAGE_SWAP_ORDER;
	img->color_type = IMAGE_COLOR_TYPE_RGB_ALPHA;
	img->iodata = f;
	img->iofn = (vfs_fread_t)fread;
	image_png_read(img);
	fclose(f);

	return img;
    }
    return NULL;
}

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
	{"seek", tcvp_seek},
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
lookup_action(xtk_widget_t *w, void *p)
{
    action_cb_t acb = NULL;
    action_data_t *ad = (action_data_t*)w->data;

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
    skin_t *skin = calloc(sizeof(*skin), 1);
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
	free(skin);
	return NULL;
    }

    return skin;
}


static xtk_widget_t*
create_skinned_background(skin_t *skin, conf_section *sec)
{
    char *file;
    int i=0;

    i += conf_getvalue(sec, "background", "%s", &file);

    if(i != 1){
	return NULL;
    }

    return(xtk_create_background(skin->window, load_image(skin->path, file)));
}


static xtk_widget_t*
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
    ad->skin = skin;

    return xtk_create_button(skin->window, x, y, load_image(skin->path, file),
			     load_image(skin->path, of),
			     load_image(skin->path, df), lookup_action, ad);
}


static int
destroy_skinned_label(xtk_widget_t *w)
{
    action_data_t *ad = (action_data_t*)w->data;
    unregister_textwidget(w, ad->data);
    return 0;
}

static xtk_widget_t*
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
    char *action = NULL, *text, *default_text;
    action_data_t *ad = calloc(sizeof(*ad), 1);
    xtk_widget_t *l;

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
    ad->skin = skin;
    l = xtk_create_label(skin->window, x, y, w, h, xoff, yoff, default_text,
			 font, color, alpha, stype, lookup_action, ad);

    register_textwidget(l, text);
    l->ondestroy = destroy_skinned_label;

    return l;
}


static int
destroy_skinned_seek_bar(xtk_widget_t *w)
{
    action_data_t *ad = (action_data_t*)w->data;
    unregister_varwidget(w, ad->data);
    return 0;
}

static xtk_widget_t*
create_skinned_seek_bar(skin_t *skin, conf_section *sec)
{
    int x, y;
    int sp_x, sp_y;
    int ep_x, ep_y;
    char *bg, *indicator, *value;
    int i=0;
    char *action = NULL;
    action_data_t *ad = calloc(sizeof(*ad), 1);
    double *position = NULL, p=0;
    xtk_widget_t *s;

    i += conf_getvalue(sec, "position", "%d %d", &x, &y);
    i += conf_getvalue(sec, "start_position", "%d %d", &sp_x, &sp_y);
    i += conf_getvalue(sec, "end_position", "%d %d", &ep_x, &ep_y);
    i += conf_getvalue(sec, "background_image", "%s", &bg);
    i += conf_getvalue(sec, "indicator_image", "%s", &indicator);
    i += conf_getvalue(sec, "value", "%s", &value);

    if(i != 9){
	return NULL;
    }

    conf_getvalue(sec, "action", "%s", &action);

    parse_variable(value, (void *)&position);
    if(!position) {
	position = &p;	
    }

    ad->action = action;
    ad->data = value;
    ad->skin = skin;

    s = xtk_create_seek_bar(skin->window, x, y, sp_x, sp_y, ep_x, ep_y,
			    load_image(skin->path, bg),
			    load_image(skin->path, indicator), *position,
			    lookup_action, ad);
    register_varwidget((xtk_widget_t *)s, ad->data);
    s->ondestroy = destroy_skinned_seek_bar;

    return s;
}


static int
destroy_skinned_state(xtk_widget_t *w)
{
    action_data_t *ad = (action_data_t*)w->data;
    unregister_textwidget(w, ad->data);
    return 0;
}

static xtk_widget_t*
create_skinned_state(skin_t *skin, conf_section *sec)
{
    int x, y;
    int ns = 0;
    image_info_t **imgs = NULL;
    char **states = NULL;
    void *c = NULL;
    int i;
    char *img, *st, def_state[512];
    action_data_t *ad = calloc(sizeof(*ad), 1);

    i = conf_getvalue(sec, "position", "%d %d", &x, &y);
    i += conf_getvalue(sec, "value", "%s", &ad->data);
    if (i != 3) {
	return NULL;
    }

    for(i = conf_nextvalue(sec, "image", &c, "%s %s", &st, &img); c;
	i = conf_nextvalue(sec, "image", &c, "%s %s", &st, &img)){
	if(i == 2) {
	    imgs = realloc(imgs, sizeof(*imgs)*(ns+1));
	    states = realloc(states, sizeof(*states)*(ns+1));
	    imgs[ns] = load_image(skin->path, img);
	    states[ns] = st;
	    ns++;
	}
    }

    conf_getvalue(sec, "action", "%s", &ad->action);
    ad->skin = skin;

    parse_text(ad->data, def_state);

    if(ns > 0) {
	xtk_widget_t *s;

	s = xtk_create_state(skin->window, x, y, ns, imgs, states, def_state,
			     lookup_action, ad);
	register_textwidget((xtk_widget_t *)s, ad->data);
	s->ondestroy = destroy_skinned_state;

	return s;
    } else {
	return NULL;
    }
}
    

static int
tcvp_replace_ui(xtk_widget_t *w, void *p)
{
    if(tcvp_open_ui(w, p) == 0) {
	tcvp_close_ui(w, p);
    }
    return 0;
}

static int
tcvp_close_ui(xtk_widget_t *w, void *p)
{
    skin_t *s = ((action_data_t *)w->data)->skin;

    free(s->path);
    free(s);
    xtk_destroy_window(w->window);

    ui_count--;
    if(ui_count == 0) {
	tcvp_quit();
    }

    return 0;
}

static int
tcvp_open_ui(xtk_widget_t *w, void *p)
{
    char *buf;
    char *uifile = ((action_data_t *)w->data)->data;
    skin_t *s = ((action_data_t *)w->data)->skin;

    buf = malloc(strlen(uifile) + strlen(s->path) + 2);
    sprintf(buf, "%s/%s", s->path, uifile);

    skin_t *skin = load_skin(buf);
    if(!skin) {
	return -1;
    }

    skin->window = xtk_create_window("TCVP", skin->width, skin->height);

    create_ui(skin);

    xtk_show_window(skin->window);

    if(tcvp_ui_tcvpx_conf_sticky != 0) {
	xtk_set_sticky(skin->window, 1);
    }

    if(tcvp_ui_tcvpx_conf_always_on_top != 0) {
	xtk_set_always_on_top(skin->window, 1);
    }

    xtk_repaint_widgets();
    xtk_draw_widgets();

    ui_count++;

    return 0;
}


extern int
create_ui(skin_t *skin)
{
    conf_section *sec;
    void *w, *s;

    w = create_skinned_background(skin, skin->config);
    if(w == NULL) {
	/* No background - No good */
	return -1;
    }

    for(s=NULL, sec = conf_nextsection(skin->config, "button", &s);
	sec != NULL; sec = conf_nextsection(skin->config, "button", &s)) {
	create_skinned_button(skin, sec);
    }

    for(s=NULL, sec = conf_nextsection(skin->config, "label", &s);
	sec != NULL; sec = conf_nextsection(skin->config, "label", &s)) {
	create_skinned_label(skin, sec);
    }

    for(s=NULL, sec = conf_nextsection(skin->config, "state", &s);
	sec != NULL; sec = conf_nextsection(skin->config, "state", &s)) {
	create_skinned_state(skin, sec);
    }

    for(s=NULL, sec = conf_nextsection(skin->config, "seek_bar", &s);
	sec != NULL; sec = conf_nextsection(skin->config, "seek_bar", &s)) {
	create_skinned_seek_bar(skin, sec);
    }

    return 0;
}

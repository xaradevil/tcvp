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


static int pl_visible = 0;

extern skin_t*
load_skin(char *skinconf)
{
    char *tmp;
    skin_t *skin = calloc(sizeof(skin_t), 1);
    int i=0;

    if(!(skin->config = conf_load_file (NULL, skinconf))){
	fprintf(stderr, "Error loading file.\n");
	exit(1);
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
create_skinned_button(skin_t *skin, conf_section *sec, action_cb_t acb)
{
    char *file, *of = NULL, *df = NULL;
    int x, y;
    int i=0;

    i += conf_getvalue(sec, "image", "%s", &file);
    i += conf_getvalue(sec, "position", "%d %d", &x, &y);

    if(i != 3){
	return NULL;
    }

    conf_getvalue(sec, "mouse_over", "%s", &of);
    conf_getvalue(sec, "pressed", "%s", &df);

    return(create_button(skin, x, y, file, of, df, acb));
}


static tclabel_t*
create_skinned_label(skin_t *skin, conf_section *sec, char *text,
		     action_cb_t acb)
{
    int x, y;
    int w, h;
    int xoff, yoff;
    char *font;
    char *color;
    int alpha;
    int stype;
    int i=0, j;

    i += conf_getvalue(sec, "position", "%d %d", &x, &y);
    i += conf_getvalue(sec, "size", "%d %d", &w, &h);
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

    if(i != 10){
	return NULL;
    }

    return(create_label(skin, x, y, w, h, xoff, yoff, text, font,
			color, alpha, stype, acb));
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
			   indicator, position, acb));
}


static tcstate_t*
create_skinned_state(skin_t *skin, conf_section *sec, char *state,
		     action_cb_t acb)
{
    int x, y;
    int ns = 0;
    char **imgs = NULL, **states = NULL;
    void *c = NULL;
    int i;
    char *img, *st;

    i = conf_getvalue(sec, "position", "%d %d", &x, &y);
    if (i != 2) {
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

    if(ns > 0) {
	return(create_state(skin, x, y, ns, imgs, states, state, acb));
    } else {
	return NULL;
    }
}
    

static int
tcvp_playlist(tcwidget_t *w, void *p)
{
    if(pl_visible == 0) {
	char *plfile = alloca(strlen(w->common.skin->path) +
			      strlen(w->common.skin->playlistfile) + 2);

	sprintf(plfile, "%s/%s", w->common.skin->path,
		w->common.skin->playlistfile);

	skin_t *pl = load_skin(plfile);
	create_window(pl);
	create_ui(pl);

	XMapWindow (xd, pl->xw);
	XMapSubwindows(xd, pl->xw);

	w->common.data = pl;

	pl_visible = 1;
    } else {
	skin_t *pl = w->common.data;

	destroy_window(pl);
	pl_visible = 0;
    }
    return 0;
}


extern int
create_ui(skin_t *skin)
{
    conf_section *sec;
    void *w;

    conf_getvalue(skin->config, "playlist", "%s", &skin->playlistfile);

    if((skin->background = create_skinned_background(skin, skin->config)) ==
       NULL) {
	/* No background - No good */
	return -1;
    }
    list_push(skin->widgets, skin->background);

    sec = conf_getsection(skin->config, "buttons/previous");
    if(sec){
	w = create_skinned_button(skin, sec, tcvp_previous);
	if(w){
	    list_push(skin->widgets, w);
	}
    }

    sec = conf_getsection(skin->config, "buttons/play");
    if(sec){
	w = create_skinned_button(skin, sec, tcvp_play);
	if(w){
	    list_push(skin->widgets, w);
	}
    }

    sec = conf_getsection(skin->config, "buttons/pause");
    if(sec){
	w = create_skinned_button(skin, sec, tcvp_pause);
	if(w){
	    list_push(skin->widgets, w);
	}
    }

    sec = conf_getsection(skin->config, "buttons/stop");
    if(sec){
	w = create_skinned_button(skin, sec, tcvp_stop);
	if(w){
	    list_push(skin->widgets, w);
	}
    }

    sec = conf_getsection(skin->config, "buttons/next");
    if(sec){
	w = create_skinned_button(skin, sec, tcvp_next);
	if(w){
	    list_push(skin->widgets, w);
	}
    }

    sec = conf_getsection(skin->config, "buttons/quit");
    if(sec){
	w = create_skinned_button(skin, sec, tcvp_close);
	if(w){
	    list_push(skin->widgets, w);
	}
    }
    
    sec = conf_getsection(skin->config, "buttons/playlist");
    if(sec){
	w = create_skinned_button(skin, sec, tcvp_playlist);
	if(w){
	    list_push(skin->widgets, w);
	}
    }

    sec = conf_getsection(skin->config, "time");
    if(sec){
	w = skin->time =
	    create_skinned_label(skin, sec, "   0:00", toggle_time);
	if(w) {
	    list_push(skin->widgets, w);
	}
    }

    sec = conf_getsection(skin->config, "title");
    if(sec){
	w = skin->title = create_skinned_label(skin, sec, "", NULL);
	if(w) {
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

    sec = conf_getsection(skin->config, "state");
    if(sec){
	w = skin->state = create_skinned_state(skin, sec, "stopped", NULL);
	if(w) {
	    list_push(skin->widgets, w);
	}
    }

    return 0;
}

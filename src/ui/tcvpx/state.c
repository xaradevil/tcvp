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

#include "tcvpx.h"
#include <string.h>

static int
repaint_state(tcwidget_t *w)
{
    if(mapped==1 && w->state.skin->enabled == 1){
	XImage *img;
	img = XGetImage(xd, w->state.skin->background->pixmap,
			w->state.x, w->state.y,
			w->state.width, w->state.height,
			AllPlanes, ZPixmap);
	alpha_render(*w->state.images[w->state.active_state]->data,
		     img->data, img->width, img->height, depth);
	XPutImage(xd, w->state.pixmap, w->background.skin->bgc, img,
		  0, 0, 0, 0, w->state.width, w->state.height);
	XSync(xd, False);
	XDestroyImage(img);
    }
    return 0;
}


static int
destroy_state(tcwidget_t *w)
{
    int i;

    for(i=0; i<w->state.num_states; i++) {
	free(*w->state.images[i]->data);
	free(w->state.images[i]->data);
	free(w->state.images[i]);
    }

    free(w->state.states);
    free(w->state.images);
    return 0;
}


extern int
change_state(tcstate_t *st, char *state)
{
    int i;
    for(i=0; i<st->num_states; i++) {
	if(strcmp(st->states[i], state) == 0){
	    st->active_state = i;
	    break;
	}
    }

    if(mapped==1){
	st->repaint((tcwidget_t *) st);
	draw_widget((tcwidget_t *) st);
	XSync(xd, False);
    }

    return 0;
}


extern tcstate_t*
create_state(skin_t *skin, int x, int y, int num_states, char **imagefiles,
	     char **states, char *state, action_cb_t action, void *data)
{
    int i;
    tcstate_t *st = calloc(sizeof(tcstate_t), 1);
    long emask;

    st->type = TCSTATE;
    st->x = x;
    st->y = y;
    st->repaint = repaint_state;
    st->destroy = destroy_state;
    st->skin = skin;
    st->num_states = num_states;
    st->states = malloc(num_states * sizeof(*st->states));
    st->images = malloc(num_states * sizeof(*st->images));
    st->data = data;
    
    for(i=0; i<num_states; i++) {
/* 	fprintf(stderr, "%s -> %s\n", states[i], imagefiles[i]); */
	st->states[i] = states[i];
	st->images[i] = load_image(skin->path, imagefiles[i]);
    }

    st->width = st->images[0]->width;
    st->height = st->images[0]->height;
    st->enabled = 1;

    st->win = XCreateWindow(xd, skin->xw, st->x, st->y,
			    st->width, st->height,
			    0, CopyFromParent, InputOutput,
			    CopyFromParent, 0, 0);
    st->pixmap = XCreatePixmap(xd, skin->xw, st->width,
			       st->height, depth);

    change_state(st, state);

    emask = ExposureMask;
    list_push(widget_list, st);
    if(action){
	st->action = action;
	st->onclick = widget_onclick;
	list_push(click_list, st);
	emask |= ButtonPressMask;
    }

    XSelectInput(xd, st->win, emask);

    return st;
}

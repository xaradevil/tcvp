/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include "widgets.h"
#include <string.h>

static int
repaint_state(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;

    if(w->state.window->mapped==1 && w->state.window->enabled == 1){
	XImage *img;
	img = XGetImage(xd, w->state.window->background->pixmap,
			w->state.x, w->state.y,
			w->state.width, w->state.height,
			AllPlanes, ZPixmap);
	if(w->state.background) {
	    alpha_render(*w->state.background->data, img->data, img->width,
			 img->height, depth);
	}
	alpha_render(*w->state.images[w->state.active_state]->data,
		     img->data, img->width, img->height, depth);
	XPutImage(xd, w->state.pixmap, w->background.window->bgc, img,
		  0, 0, 0, 0, w->state.width, w->state.height);
	XSync(xd, False);
	XDestroyImage(img);
    }
    return 0;
}


static int
destroy_state(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    int i;

    for(i=0; i<w->state.num_states; i++) {
	image_free(w->state.images[i]);
	free(w->state.images[i]);
	free(w->state.states[i]);
    }

    if(w->state.background) {
	image_free(w->state.background);
	free(w->state.background);
    }

    free(w->state.states);
    free(w->state.images);
    return 0;
}


extern int
change_state(xtk_widget_t *xst, char *state)
{
    tcstate_t *st = (tcstate_t *) xst;
    int i;
    for(i=0; i<st->num_states; i++) {
	if(strcmp(st->states[i], state) == 0){
	    st->active_state = i;
	    break;
	}
    }

    if(st->window->mapped==1){
	st->repaint((xtk_widget_t *) st);
	draw_widget((tcwidget_t *) st);
	XSync(xd, False);
    }

    return 0;
}


extern xtk_widget_t*
create_state(window_t *window, int x, int y, image_info_t *bg,
	     int num_states, image_info_t **images, char **states,
	     char *state, action_cb_t action, void *data)
{
    int i;
    tcstate_t *st = calloc(sizeof(tcstate_t), 1);
    long emask;

    st->type = TCSTATE;
    st->x = x;
    st->y = y;
    st->repaint = repaint_state;
    st->destroy = destroy_state;
    st->window = window;
    st->background = bg;
    st->num_states = num_states;
    st->states = malloc(num_states * sizeof(*st->states));
    st->images = malloc(num_states * sizeof(*st->images));
    st->data = data;
    
    st->width = images[0]->width;
    st->height = images[0]->height;
    st->enabled = 1;

    st->win = XCreateWindow(xd, window->xw, st->x, st->y,
			    st->width, st->height,
			    0, CopyFromParent, InputOutput,
			    CopyFromParent, 0, 0);
    st->pixmap = XCreatePixmap(xd, window->xw, st->width,
			       st->height, depth);
    clear_shape(st->win);

    for(i=0; i<num_states; i++) {
/* 	fprintf(stderr, "%s -> %s\n", states[i], imagefiles[i]); */
	st->states[i] = strdup(states[i]);
	st->images[i] = images[i];
	shape_window(st->win, images[i], ShapeUnion, NULL);
    }

    merge_shape(window, st->win, x, y);
    change_state((xtk_widget_t *) st, state);

    emask = ExposureMask;
    tclist_push(widget_list, st);
    if(action){
	st->action = action;
	st->onclick = widget_onclick;
	tclist_push(click_list, st);
	emask |= ButtonPressMask | ButtonReleaseMask | EnterWindowMask |
	    LeaveWindowMask;
    }

    XSelectInput(xd, st->win, emask);

    tclist_push(window->widgets, st);

    return (xtk_widget_t *) st;
}

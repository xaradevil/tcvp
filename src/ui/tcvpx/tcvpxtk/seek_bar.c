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

#include "widgets.h"

#define LABEL_PRESS (1<<0)
#define LABEL_DRAG  (1<<1)

extern int
repaint_seek_bar(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if(w->state.window->mapped==1){
	if(w->label.window->enabled == 1) {
	    XImage *img;
	    int xp,yp;

	    img = XGetImage(xd, w->seek_bar.window->background->pixmap,
			    w->seek_bar.x, w->seek_bar.y,
			    w->seek_bar.width, w->seek_bar.height,
			    AllPlanes, ZPixmap);
	    alpha_render(*w->seek_bar.background->data, img->data,
			 img->width, img->height, depth);

	    if(w->seek_bar.enabled == 1){
		xp=w->seek_bar.start_x +
		    (w->seek_bar.position *
		     (w->seek_bar.end_x - w->seek_bar.start_x)) - 
		    w->seek_bar.indicator->width/2;
		yp=w->seek_bar.start_y +
		    (w->seek_bar.position *
		     (w->seek_bar.end_y - w->seek_bar.start_y)) -
		    w->seek_bar.indicator->height/2;

		alpha_render_part(*w->seek_bar.indicator->data, img->data,
				  0, 0, xp, yp,
				  w->seek_bar.indicator->width,
				  w->seek_bar.indicator->height,
				  w->seek_bar.width, w->seek_bar.height,
				  depth);
	    }

	    XPutImage(xd, w->seek_bar.pixmap, w->background.window->bgc, img,
		      0, 0, 0, 0, w->seek_bar.width, w->seek_bar.height);
	    XSync(xd, False);
	    XDestroyImage(img);
	}
    }
    return 0;
}


extern int
disable_seek_bar(xtk_widget_t *xw)
{
    tcseek_bar_t *w = (tcseek_bar_t *) xw;

    if(w->enabled == 1) {
	w->enabled = 0;
	w->repaint((xtk_widget_t *) w);
	draw_widget((tcwidget_t *) w);
	XSync(xd, False);
    }

    return 0;
}


extern int
enable_seek_bar(xtk_widget_t *xw)
{
    tcseek_bar_t *w = (tcseek_bar_t *) xw;

    if(w->enabled == 0) {
	w->enabled = 1;
	w->repaint((xtk_widget_t *) w);
	draw_widget((tcwidget_t *) w);
	XSync(xd, False);
    }

    return 0;
}


extern int
change_seek_bar(xtk_widget_t *xw, double position)
{
    tcseek_bar_t *w = (tcseek_bar_t *) xw;

    if(w->enabled && (w->state & LABEL_DRAG) == 0) {
	w->position = position;
	w->repaint((xtk_widget_t *) w);
	draw_widget((tcwidget_t *) w);
	XSync(xd, False);
    }

    return 0;
}


static int
sb_ondrag(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;

    int xc = ((XEvent *)xe)->xbutton.x;
    int yc = ((XEvent *)xe)->xbutton.y;
    int xl = w->seek_bar.end_x - w->seek_bar.start_x;
    int yl = w->seek_bar.end_y - w->seek_bar.start_y;
    int xs = w->seek_bar.start_x;
    int ys = w->seek_bar.start_y;
    int x=0, y=0;

    if(xl>0) {
	x = xc-xs;
	x = (x<0)?0:(x<=xl)?x:xl;
	w->seek_bar.position = (double)x/xl;
    } else if(yl>0) {
	y = yc-ys;	
	y = (y<0)?0:(y<=yl)?y:yl;
	w->seek_bar.position = (double)y/yl;
    }

    w->seek_bar.repaint((xtk_widget_t *)w);
    draw_widget(w);
    XSync(xd, False);

    return 0;
}

static int
seek_bar_drag_begin(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    w->seek_bar.state |= LABEL_DRAG;

    sb_ondrag(xw, xe);

    return 0;
}

static int
seek_bar_drag_end(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;

    sb_ondrag(xw, xe);

    w->seek_bar.state &= ~LABEL_DRAG;

    w->seek_bar.action((xtk_widget_t *)w, &w->seek_bar.position);
    return 0;
}



static int
press_sb(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    w->seek_bar.indicator = w->seek_bar.down_img;
    repaint_seek_bar(xw);
    draw_widget(w);
    w->seek_bar.state |= LABEL_PRESS;
    return 0;
}


static int
release_sb(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if(w->seek_bar.indicator == w->seek_bar.down_img) {
	if(w->seek_bar.over_img)
	    w->seek_bar.indicator = w->seek_bar.over_img;
	else
	    w->seek_bar.indicator = w->seek_bar.standard_img;
    }
    repaint_seek_bar(xw);
    draw_widget(w);
    w->seek_bar.state &= ~LABEL_PRESS;
    return 0;
}


static int
enter_sb(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if((w->seek_bar.state & LABEL_PRESS) == 0) {
	w->seek_bar.indicator = w->seek_bar.over_img;
	repaint_seek_bar(xw);
	draw_widget(w);
    }
    return 0;
}


static int
exit_sb(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if((w->seek_bar.state & LABEL_PRESS) == 0) {
	w->seek_bar.indicator = w->seek_bar.standard_img;
	repaint_seek_bar(xw);
	draw_widget(w);
    }
    return 0;
}


int
destroy_seek_bar(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;

    image_free(w->seek_bar.background);
    free(w->seek_bar.background);

    image_free(w->seek_bar.standard_img);
    free(w->seek_bar.standard_img);

    if(w->seek_bar.over_img) {
	image_free(w->seek_bar.over_img);
	free(w->seek_bar.over_img);
    }

    if(w->seek_bar.down_img) {
	image_free(w->seek_bar.down_img);
	free(w->seek_bar.down_img);
    }

    return 0;
}


extern xtk_widget_t*
create_seek_bar(window_t *window, int x, int y, int sp_x, int sp_y,
		int ep_x, int ep_y, image_info_t *background,
		image_info_t *indicator, image_info_t *over_image,
		image_info_t *down_image, double position,
		action_cb_t action, void *data)
{
    tcseek_bar_t *sb;
    long emask;

    if(!background || !indicator) return NULL;

    sb = calloc(sizeof(tcseek_bar_t), 1);
    sb->type = TCSEEKBAR;
    sb->x = x;
    sb->y = y;
    sb->start_x = sp_x;
    sb->start_y = sp_y;
    sb->end_x = ep_x;
    sb->end_y = ep_y;
    sb->repaint = repaint_seek_bar;
    sb->destroy = destroy_seek_bar;
    sb->window = window;
    sb->position = position;
    sb->background = background;
    sb->indicator = sb->standard_img = indicator;
    sb->width = sb->background->width;
    sb->height = sb->background->height;
    sb->enabled = 1;
    sb->data = data;

    sb->win = XCreateWindow(xd, window->xw, sb->x, sb->y,
			     sb->width, sb->height,
			     0, CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);
    sb->pixmap = XCreatePixmap(xd, window->xw, sb->width,
				sb->height, depth);

    emask = ExposureMask;
    list_push(widget_list, sb);


    if(action){
	sb->action = action;
	sb->drag_begin = seek_bar_drag_begin;
	sb->ondrag = sb_ondrag;
	sb->drag_end = seek_bar_drag_end;
	emask |= ButtonPressMask | PointerMotionMask | ButtonReleaseMask;
	list_push(click_list, sb);
    }

    if(over_image != NULL) {
	sb->over_img = over_image;
	emask |= EnterWindowMask | LeaveWindowMask;
	sb->enter = enter_sb;
	sb->exit = exit_sb;
    }
    if(down_image != NULL) {
	sb->down_img = down_image;
	emask |= ButtonPressMask | ButtonReleaseMask;
	sb->press = press_sb;
	sb->release = release_sb;
    }

    XSelectInput(xd, sb->win, emask);

    list_push(window->widgets, sb);

    return (xtk_widget_t *) sb;
}

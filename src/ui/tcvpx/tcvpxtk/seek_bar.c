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
disable_seek_bar(tcseek_bar_t *w)
{
    if(w->enabled == 1) {
	w->enabled = 0;
	w->repaint((xtk_widget_t *) w);
	draw_widget((tcwidget_t *) w);
	XSync(xd, False);
    }

    return 0;
}


extern int
enable_seek_bar(tcseek_bar_t *w)
{
    if(w->enabled == 0) {
	w->enabled = 1;
	w->repaint((xtk_widget_t *) w);
	draw_widget((tcwidget_t *) w);
	XSync(xd, False);
    }

    return 0;
}


extern int
change_seek_bar(tcseek_bar_t *w, double position)
{
    if(w->enabled) {
	w->position = position;
	w->repaint((xtk_widget_t *) w);
	draw_widget((tcwidget_t *) w);
	XSync(xd, False);
    }

    return 0;
}


extern int
seek_bar_onclick(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;

    int xc = ((XEvent *)xe)->xbutton.x;
    int yc = ((XEvent *)xe)->xbutton.y;
    int xl = w->seek_bar.end_x - w->seek_bar.start_x;
    int yl = w->seek_bar.end_y - w->seek_bar.start_y;
    int xs = w->seek_bar.start_x;
    int ys = w->seek_bar.start_y;
    double pos;
    int x=0, y=0;

    if(xl>0) {
	x = xc-xs;
	x = (x<0)?0:(x<=xl)?x:xl;
	pos = (double)x/xl;
    } else if(yl>0) {
	y = yc-ys;	
	y = (y<0)?0:(y<=yl)?y:yl;
	pos = (double)y/yl;
    }

/*     fprintf(stderr, "seek bar clicked (%d,%d)->%f\n", x, y, pos); */

    w->seek_bar.action((xtk_widget_t *)w, &pos);
    return 0;
}


int
destroy_seek_bar(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;

    free(*w->seek_bar.background->data);
    free(w->seek_bar.background->data);
    free(w->seek_bar.background);

    free(*w->seek_bar.indicator->data);
    free(w->seek_bar.indicator->data);
    free(w->seek_bar.indicator);

    return 0;
}


extern tcseek_bar_t*
create_seek_bar(window_t *window, int x, int y, int sp_x, int sp_y,
		int ep_x, int ep_y, image_info_t *background,
		image_info_t *indicator, double position,
		action_cb_t action, void *data)
{
    tcseek_bar_t *sb = calloc(sizeof(tcseek_bar_t), 1);
    long emask;

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
    sb->indicator = indicator;
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
	sb->onclick = seek_bar_onclick;
	sb->action = action;
	list_push(click_list, sb);
	emask |= ButtonPressMask;
    }

    XSelectInput(xd, sb->win, emask);

    list_push(window->widgets, sb);

    return sb;
}

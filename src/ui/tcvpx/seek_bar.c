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

extern int
repaint_seek_bar(tcwidget_t *w)
{
    if(mapped==1){
	XImage *img;
	int xp,yp;

	img = XGetImage(xd, w->seek_bar.skin->background->pixmap,
			w->seek_bar.x, w->seek_bar.y,
			w->seek_bar.width, w->seek_bar.height,
			AllPlanes, ZPixmap);
	alpha_render(*w->seek_bar.background->data, img->data,
		     img->width, img->height, depth);

	if(w->seek_bar.enabled == 1){
	    xp=w->seek_bar.start_x +
		(w->seek_bar.position * (w->seek_bar.end_x - w->seek_bar.start_x));
	    yp=w->seek_bar.start_y +
		(w->seek_bar.position * (w->seek_bar.end_y - w->seek_bar.start_y));

	    alpha_render_part(*w->seek_bar.indicator->data, img->data,
			      0, 0, xp, yp,
			      w->seek_bar.indicator->width,
			      w->seek_bar.indicator->height,
			      w->seek_bar.width, w->seek_bar.height,
			      depth);
	}

	XPutImage(xd, w->seek_bar.pixmap, bgc, img, 0, 0, 0, 0,
		  w->seek_bar.width, w->seek_bar.height);
	XSync(xd, False);
	XDestroyImage(img);
    }
    return 0;
}


extern int
disable_seek_bar(tcseek_bar_t *w)
{
    w->enabled = 0;
    w->repaint((tcwidget_t *) w);
    draw_widget((tcwidget_t *) w);
    XSync(xd, False);

    return 0;
}


extern int
enable_seek_bar(tcseek_bar_t *w)
{
    w->enabled = 1;
    w->repaint((tcwidget_t *) w);
    draw_widget((tcwidget_t *) w);
    XSync(xd, False);

    return 0;
}


extern int
change_seek_bar(tcseek_bar_t *w, double position)
{
    w->position = position;
    w->repaint((tcwidget_t *) w);
    draw_widget((tcwidget_t *) w);
    XSync(xd, False);

    return 0;
}


extern tcseek_bar_t*
create_seek_bar(skin_t *skin, int x, int y, int sp_x, int sp_y,
		int ep_x, int ep_y, char *background, char *indicator,
		double position, onclick_cb_t onclick)
{
    tcseek_bar_t *sb = calloc(sizeof(tcseek_bar_t), 1);

    sb->type = TCSEEKBAR;
    sb->x = x;
    sb->y = y;
    sb->start_x = sp_x;
    sb->start_y = sp_y;
    sb->end_x = ep_x;
    sb->end_y = ep_y;
    sb->onclick = onclick;
    sb->repaint = repaint_seek_bar;
    sb->skin = skin;
    sb->position = position;
    sb->background = load_image(skin->path, background);
    sb->indicator = load_image(skin->path, indicator);
    sb->width = sb->background->width;
    sb->height = sb->background->height;
    sb->enabled = 1;

    sb->win = XCreateWindow(xd, xw, sb->x, sb->y,
			     sb->width, sb->height,
			     0, CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);
    sb->pixmap = XCreatePixmap(xd, xw, sb->width,
				sb->height, depth);

    list_push(widget_list, sb);
    if(onclick){
	list_push(bt_list, sb);
	XSelectInput(xd, sb->win, ButtonPressMask);
    }

    return sb;
}

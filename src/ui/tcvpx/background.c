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
#include <X11/extensions/shape.h>
#include <string.h>

extern int
update_root(skin_t *skin)
{
    if(skin->background->transparent){
	XImage *img;
	img = XGetImage(xd, RootWindow(xd, xs), 0, 0, root_width, 
			root_height, AllPlanes, ZPixmap);
	XPutImage(xd, root, bgc, img, 0, 0, 0, 0, root_width, root_height);
	XSync(xd, False);

	XDestroyImage(img);
    }

    return 0;
}


extern int
repaint_background(tcwidget_t *w)
{
    XImage *img;

    if(w->background.transparent){
	XWindowAttributes wa;
	Window foo;
	int x, y;

	XGetWindowAttributes(xd, w->background.win, &wa);
	XTranslateCoordinates(xd, w->background.win, RootWindow(xd, xs),
			      wa.x, wa.y, &x, &y, &foo);

	if(x>=0 && y>=0 && x+w->background.width < root_width &&
	   y+w->background.height < root_height) {
	    img = XGetImage(xd, root, x, y, w->background.width,
			    w->background.height, AllPlanes, ZPixmap);
	} else {
	    Pixmap tmp_p;
	    XImage *tmp_i;
	    int tmp_x, tmp_y, tmp_w, tmp_h, xp, yp;

	    tmp_p = XCreatePixmap(xd, xw, w->background.width,
				  w->background.height, depth);
	    if(x<0) {
		tmp_x = 0;
		xp = -x;
		tmp_w = w->background.width + x;
	    } else if(x+w->background.width >= root_width){ 
		tmp_x = x;
		xp = 0;
		tmp_w = w->background.width -
		    ((x + w->background.width) - root_width);
	    } else { 
		tmp_x = x;
		xp = 0;
		tmp_w = w->background.width;
	    }

	    if(y<0) {
		tmp_y = 0;
		yp = -y;
		tmp_h = w->background.height + y;
	    } else if(y+w->background.height >= root_height){
		tmp_y = y;
		yp = 0;
		tmp_h = w->background.height -
		    ((y + w->background.height) - root_height);
	    } else {
		tmp_y = y;
		yp = 0;
		tmp_h = w->background.height;
	    }

	    tmp_i = XGetImage(xd, root, tmp_x, tmp_y, tmp_w, tmp_h,
			      AllPlanes, ZPixmap);
	    XPutImage(xd, tmp_p, bgc, tmp_i, 0, 0, xp, yp, tmp_w, tmp_h);
	    XSync(xd, False);
	    XDestroyImage(tmp_i);
	    img = XGetImage(xd, tmp_p, 0, 0, w->background.width,
			    w->background.height, AllPlanes, ZPixmap);
	    XSync(xd, False);
	    XFreePixmap(xd, tmp_p);
	}

	alpha_render(*w->background.img->data, img->data, w->background.width,
		     w->background.height, depth);

	XPutImage(xd, w->background.pixmap, bgc, img, 0, 0, 0, 0,
		  w->background.width, w->background.height);
	XSync(xd, False);

	XDestroyImage(img);
    } else {
	img = XGetImage(xd, w->background.pixmap, 0, 0, w->background.width,
			w->background.height, AllPlanes, ZPixmap);

	memcpy(img->data, *w->background.img->data,
	       w->background.width * w->background.height * 4);

	XPutImage(xd, w->background.pixmap, bgc, img, 0, 0, 0, 0,
		  w->background.width, w->background.height);

	XSync(xd, False);
	XDestroyImage(img);
	
    }

    return 0;
}

extern tcbackground_t*
create_background(skin_t *skin, char *imagefile)
{
    char *data;
    Pixmap maskp;
    tcbackground_t *bg = calloc(sizeof(tcbackground_t), 1);
    int x, y;

    bg->type = TCBACKGROUND;
    bg->x = 0;
    bg->y = 0;
    bg->onclick = NULL;
    bg->repaint = repaint_background;
    bg->skin = skin;
    bg->img = load_image(skin->path, imagefile);
    bg->width = bg->img->width;
    bg->height = bg->img->height;
    bg->transparent = 0;
    bg->pixmap = XCreatePixmap(xd, xw, bg->width,
			       bg->height, depth);
    bg->win = xw;

    data = calloc(bg->width * bg->height,1);
    for(y=0; y<bg->height; y++){
	for(x=0; x<bg->width; x+=8){
	    int i, d=0;
	    for(i=0;i<8;i++){
		d |= (bg->img->data[y][(x+i)*4+3]==0)?0:1<<i;
		if(bg->img->data[y][(x+i)*4+3]>0 &&
		   bg->img->data[y][(x+i)*4+3]<255){
		    bg->transparent = 1;
		}
	    }
	    data[x/8+y*bg->width/8] = d;
	}
    }

    maskp = XCreateBitmapFromData(xd, xw, data, bg->width, bg->height);
    free(data);
    XShapeCombineMask(xd, xw, ShapeBounding, 0, 0, maskp, ShapeSet);
    XSync(xd, False);
    XFreePixmap(xd, maskp);

    list_push(widget_list, bg);

    return bg;
}

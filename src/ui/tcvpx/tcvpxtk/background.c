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
#include <X11/extensions/shape.h>
#include <string.h>


static Pixmap
get_root(window_t *window)
{
    Atom xa_pmap = XInternAtom(xd, "PIXMAP", True);
    Atom aret;
    int fret;
    unsigned long nitems, remain;
    unsigned char *buf;
    Pixmap pmap;

    XGetWindowProperty(xd, RootWindow(xd, xs), window->background->xa_rootpmap,
		       0, 1, False, xa_pmap, &aret, &fret, &nitems,
		       &remain, &buf);

    pmap = *((Pixmap*)buf);

    XFree(buf);

    return pmap;
}

static int
check_root(window_t *window)
{
    if(window->background->xa_rootpmap != None) {
	root = get_root(window);
    }

    return 0;
}

extern int
update_root(window_t *window)
{
    if(window->background->transparent){
	window->background->xa_rootpmap =
	    XInternAtom(xd, "_XROOTPMAP_ID", True);

	if(window->background->xa_rootpmap != None) {
	    root = get_root(window);
	} else {	
	    XImage *img;
	    img = XGetImage(xd, RootWindow(xd, xs), 0, 0, root_width, 
			    root_height, AllPlanes, ZPixmap);
	    XPutImage(xd, root, window->bgc, img, 0, 0, 0, 0, root_width,
		      root_height);
	    XSync(xd, False);

	    XDestroyImage(img);
	}
	XSync(xd, False);
    }

    return 0;
}


extern int
repaint_background(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    XImage *img;
    GC bgc = w->background.window->bgc;

    if(w->background.window->subwindow) {
	if(w->background.transparent){
	    img = XGetImage(xd, w->background.window->parent->background->pixmap,
			    w->background.window->x, w->background.window->y,
			    w->background.width, w->background.height,
			    AllPlanes, ZPixmap);

	    XSync(xd, False);

	    alpha_render(*w->background.img->data, img->data,
			 w->background.width, w->background.height, depth);

	    XPutImage(xd, w->background.pixmap, bgc, img, 0, 0, 0, 0,
		      w->background.width, w->background.height);
	    XSync(xd, False);

	    XDestroyImage(img);
	} else {
	    img = XGetImage(xd, w->background.pixmap, 0, 0,
			    w->background.width, w->background.height,
			    AllPlanes, ZPixmap);

	    memcpy(img->data, *w->background.img->data,
		   w->background.width * w->background.height * 4);

	    XPutImage(xd, w->background.pixmap, bgc, img, 0, 0, 0, 0,
		      w->background.width, w->background.height);

	    XSync(xd, False);
	    XDestroyImage(img);
	}

    } else {
	if(w->background.transparent){
	    XWindowAttributes wa;
	    Window foo;
	    int x, y;

	    XLockDisplay(xd);
	    check_root(w->background.window);

	    XGetWindowAttributes(xd, w->background.win, &wa);
	    XTranslateCoordinates(xd, w->background.win, RootWindow(xd, xs),
				  wa.x, wa.y, &x, &y, &foo);

	    if(x > w->background.width && x <= -w->background.width && 
	       y > w->background.height && y <= -w->background.height)
		return 0;

	    if(x>=0 && y>=0 && x+w->background.width < root_width &&
	       y+w->background.height < root_height) {
		img = XGetImage(xd, root, x, y, w->background.width,
				w->background.height, AllPlanes, ZPixmap);
	    } else {
		int tmp_x, tmp_y, tmp_w, tmp_h, xp, yp;
		Pixmap pmap;

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

		pmap = XCreatePixmap(xd, w->background.window->xw,
				     w->background.width,
				     w->background.height, depth);

		XCopyArea(xd, root, pmap, bgc, tmp_x, tmp_y, tmp_w, tmp_h,
			  xp, yp);
		img = XGetImage(xd, pmap, 0, 0, w->background.width,
				w->background.height,
				AllPlanes, ZPixmap);

		XSync(xd, False);
		XFreePixmap(xd, pmap);
	    }
	    XSync(xd, False);

	    alpha_render(*w->background.img->data, img->data,
			 w->background.width, w->background.height, depth);

	    XPutImage(xd, w->background.pixmap, bgc, img, 0, 0, 0, 0,
		      w->background.width, w->background.height);
	    XSync(xd, False);

	    XDestroyImage(img);

	    XUnlockDisplay(xd);
	} else {
	    img = XGetImage(xd, w->background.pixmap, 0, 0,
			    w->background.width, w->background.height,
			    AllPlanes, ZPixmap);

	    memcpy(img->data, *w->background.img->data,
		   w->background.width * w->background.height * 4);

	    XPutImage(xd, w->background.pixmap, bgc, img, 0, 0, 0, 0,
		      w->background.width, w->background.height);

	    XSync(xd, False);
	    XDestroyImage(img);
	}
    }

    return 0;
}


static int
destroy_background(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;

    image_free(w->background.img);
    free(w->background.img);

    w->background.window->background = NULL;
    return 0;
}


extern xtk_widget_t*
create_background(window_t *window, image_info_t *image)
{
    char *data;
    Pixmap maskp;
    int x, y;
    tcbackground_t *bg;

    if(!image) return NULL;

    bg = calloc(sizeof(tcbackground_t), 1);

    bg->type = TCBACKGROUND;
    bg->x = 0;
    bg->y = 0;
    bg->onclick = NULL;
    bg->repaint = repaint_background;
    bg->destroy = destroy_background;
    bg->window = window;
    bg->img = image;
    bg->width = bg->img->width;
    bg->height = bg->img->height;
    bg->transparent = 0;
    bg->pixmap = XCreatePixmap(xd, window->xw, bg->width,
			       bg->height, depth);
    bg->win = window->xw;
    bg->enabled = 1;

    data = calloc(bg->width * bg->height, 1);
    for(y=0; y<bg->height; y++){
	for(x=0; x<bg->width; x+=8){
	    int i, d=0;
	    for(i=0;i<8 && x+i<bg->width;i++){
		d |= (bg->img->data[y][(x+i)*4+3]==0)?0:1<<i;
		if(bg->img->data[y][(x+i)*4+3]>0 &&
		   bg->img->data[y][(x+i)*4+3]<255){
		    bg->transparent = 1;
		}
	    }
	    data[x/8+y*bg->width/8] = d;
	}
    }

    if(!window->subwindow) {
	maskp = XCreateBitmapFromData(xd, window->xw, data, bg->width,
				      bg->height);
	XShapeCombineMask(xd, window->xw, ShapeBounding, 0, 0, maskp,
			  ShapeSet);
	XSync(xd, False);
	XFreePixmap(xd, maskp);
    }
    free(data);

    list_push(widget_list, bg);

    if(window->background) {
	free(window->background);
    }
    window->background = bg;

    list_push(window->widgets, bg);

    return (xtk_widget_t *) bg;
}

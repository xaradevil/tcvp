/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
**/

#include "widgets.h"

static int
repaint_box(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if(w->box.window->mapped==1 && w->box.window->enabled == 1){
	XCopyArea(xd, w->box.window->background->pixmap, w->box.pixmap,
		  w->background.window->bgc, w->box.x, w->box.y,
		  w->box.width, w->box.height, 0, 0);
    }
    return 0;
}


extern window_t*
box_get_subwindow(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;

    return w->box.subwindow;
}

extern xtk_widget_t*
create_box(window_t *window, int x, int y, int width, int height,
	   void *data)
{
    int emask = 0;
    tcbox_t *box = calloc(sizeof(tcbox_t), 1);

    box->type = TCBOX;
    box->x = x;
    box->y = y;
    box->repaint = repaint_box;
    box->window = window;
    box->width = width;
    box->height = height;
    box->enabled = 1;
    box->data = data;

    box->win = XCreateWindow(xd, window->xw, box->x, box->y,
			     box->width, box->height,
			     0, CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);
    box->pixmap = XCreatePixmap(xd, window->xw, box->width,
				box->height, depth);
    clear_shape(box->win);

    box->subwindow = calloc(sizeof(*box->subwindow), 1);
    box->subwindow->xw = box->win;
    box->subwindow->x = x;
    box->subwindow->y = y;
    box->subwindow->width = width;
    box->subwindow->height = height;
    box->subwindow->enabled = 1;
    box->subwindow->subwindow = 1;
    box->subwindow->widgets = tclist_new(TC_LOCK_SLOPPY);

    box->subwindow->bgc = XCreateGC (xd, box->subwindow->xw, 0, NULL);
    XSetBackground(xd, box->subwindow->bgc, 0x00000000);
    XSetForeground(xd, box->subwindow->bgc, 0xFFFFFFFF);

    box->subwindow->wgc = XCreateGC (xd, box->subwindow->xw, 0, NULL);
    XSetBackground(xd, box->subwindow->bgc, 0xFFFFFFFF);
    XSetForeground(xd, box->subwindow->bgc, 0x00000000);

    box->subwindow->net_wm_support = window->net_wm_support;

    box->subwindow->background = calloc(sizeof(*box->subwindow->background),1);
    box->subwindow->background->pixmap = box->pixmap;
    box->subwindow->background->width = width;
    box->subwindow->background->height = height;
    box->subwindow->parent = window;

    tclist_push(widget_list, box);
    emask |= ExposureMask;

    XSelectInput(xd, box->win, emask);

    tclist_push(window->widgets, box);

    return (xtk_widget_t *) box;
}

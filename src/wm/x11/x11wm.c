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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <pthread.h>
#include <semaphore.h>
#include <tcvp_types.h>
#include <wm_x11_tc2.h>

typedef struct x11_wm {
    Display *dpy;
    Window win, swin;
    int width, height;
    float aspect;
    wm_update_t update;
    void *cbd;
    pthread_t eth;
    int color_key;
    int flags;
} x11_wm_t;

static void *
x11_event(void *p)
{
    x11_wm_t *xwm = p;
    int run = 1;

    XSelectInput(xwm->dpy, xwm->win, StructureNotifyMask);
    XMapWindow(xwm->dpy, xwm->win);
    XMapSubwindows(xwm->dpy, xwm->win);

    while(run){
	XEvent xe;

	XNextEvent(xwm->dpy, &xe);
	switch(xe.type){
	case ConfigureNotify: {
	    XWindowAttributes xwa;
	    Window foo;
	    int x, y, w, h;
	    int ckx, cky;
	    float wa;

	    XGetWindowAttributes(xwm->dpy, xwm->win, &xwa);
	    if(xwm->flags & WM_ABSCOORD){
		XTranslateCoordinates(xwm->dpy, xwm->win, xwa.root, 0, 0,
				      &x, &y, &foo);
	    } else {
		x = 0;
		y = 0;
	    }

	    wa = (float) xwa.width / xwa.height;
	    if(wa > xwm->aspect){
		h = xwa.height;
		w = h * xwm->aspect;
		ckx = (xwa.width - w) / 2;
		x += ckx;
		cky = 0;
	    } else {
		w = xwa.width;
		h = w / xwm->aspect;
		cky = (xwa.height - h) / 2;
		y += cky;
		ckx = 0;
	    }

	    XMoveResizeWindow(xwm->dpy, xwm->swin, ckx, cky, w, h);
	    xwm->update(xwm->cbd, WM_MOVE, x, y, w, h);
	    break;
	}
	case MapNotify: {
	    xwm->update(xwm->cbd, WM_SHOW, 0, 0, 0, 0);
	    break;
	}
	case UnmapNotify: {
	    xwm->update(xwm->cbd, WM_HIDE, 0, 0, 0, 0);
	    break;
	}
	case DestroyNotify:
	    run = 0;
	    break;
	}
    }

    return NULL;
}

static int
x11_close(window_manager_t *wm)
{
    x11_wm_t *xwm = wm->private;

    XDestroyWindow(xwm->dpy, xwm->win);
    XSync(xwm->dpy, False);

    pthread_join(xwm->eth, NULL);

    XCloseDisplay(xwm->dpy);
    free(xwm);
    free(wm);

    return 0;
}

static void
x11_hidecursor(x11_wm_t *xwm)
{
    Cursor crs;
    Pixmap pm;
    XColor black;
    char data[8] = { [0 ... 7] = 0 };

    XAllocNamedColor(xwm->dpy,
		     DefaultColormap(xwm->dpy, DefaultScreen(xwm->dpy)),
		     "black", &black, &black);	
    pm = XCreateBitmapFromData(xwm->dpy, xwm->win, data, 8, 8);    
    crs = XCreatePixmapCursor(xwm->dpy, pm, pm, &black, &black, 0, 0);
    XDefineCursor(xwm->dpy, xwm->win, crs);
    XFreePixmap(xwm->dpy, pm);
}

extern window_manager_t *
x11_open(int width, int height, wm_update_t upd, void *cbd,
	 conf_section *cs, int flags)
{
    window_manager_t *wm;
    x11_wm_t *xwm;
    Display *dpy;
    Window win;
    XGCValues gcv;
    char *display = NULL;

    if(cs)
	conf_getvalue(cs, "video/device", "%s", &display);

    XInitThreads();

    if((dpy = XOpenDisplay(display)) == NULL)
	return NULL;

    win = XCreateWindow(dpy, RootWindow(dpy, DefaultScreen(dpy)),
			0, 0, width, height, 0, CopyFromParent,
			InputOutput, CopyFromParent, 0, NULL);
    XSetWindowBackground(dpy, win, 0);

    xwm = calloc(1, sizeof(*xwm));
    xwm->dpy = dpy;
    xwm->win = win;
    xwm->width = width;
    xwm->height = height;
    xwm->aspect = (float) width / height;
    xwm->update = upd;
    xwm->cbd = cbd;
    xwm->flags = flags;
    conf_getvalue(cs, "video/color_key", "%i", &xwm->color_key);

    x11_hidecursor(xwm);
    xwm->swin = XCreateWindow(dpy, win, 0, 0, width, height, 0, CopyFromParent,
			      InputOutput, CopyFromParent, 0, NULL);
    XSetWindowBackground(dpy, xwm->swin, xwm->color_key);

    wm = malloc(sizeof(*wm));
    wm->close = x11_close;
    wm->private = xwm;

    pthread_create(&xwm->eth, NULL, x11_event, xwm);

    return wm;
}

extern int
x11_getwindow(window_manager_t *wm, Display **dpy, Window *win)
{
    x11_wm_t *xwm = wm->private;
    *dpy = xwm->dpy;
    *win = xwm->swin;
    return 0;
}

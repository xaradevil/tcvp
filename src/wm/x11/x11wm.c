/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

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
    Window win;
    wm_update_t update;
    void *cbd;
    pthread_t eth;
} x11_wm_t;

static void *
x11_event(void *p)
{
    x11_wm_t *xwm = p;
    int run = 1;

    XSelectInput(xwm->dpy, xwm->win, StructureNotifyMask);
    XMapWindow(xwm->dpy, xwm->win);

    while(run){
	XEvent xe;
	XConfigureEvent *xce = &xe.xconfigure;

	XNextEvent(xwm->dpy, &xe);
	switch(xe.type){
	case ConfigureNotify: {
	    xwm->update(xwm->cbd, WM_MOVE,
			xce->x, xce->y, xce->width, xce->height);
	    break;
	}
	case MapNotify: {
	    XWindowAttributes xwa;
	    XGetWindowAttributes(xwm->dpy, xwm->win, &xwa);
	    xwm->update(xwm->cbd, WM_SHOW,
			xwa.x, xwa.y, xwa.width, xwa.height);
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

extern window_manager_t *
x11_open(int width, int height, wm_update_t upd, void *cbd, conf_section *cs)
{
    window_manager_t *wm;
    x11_wm_t *xwm;
    Display *dpy;
    Window win;

    XInitThreads();

    if((dpy = XOpenDisplay(NULL)) == NULL)
	return NULL;

    win = XCreateWindow(dpy, RootWindow(dpy, DefaultScreen(dpy)),
			0, 0, width, height, 0, CopyFromParent,
			InputOutput, CopyFromParent, 0, NULL);

    xwm = calloc(1, sizeof(*xwm));
    xwm->dpy = dpy;
    xwm->win = win;
    xwm->update = upd;
    xwm->cbd = cbd;

    wm = malloc(sizeof(*wm));
    wm->close = x11_close;
    wm->private = xwm;

    pthread_create(&xwm->eth, NULL, x11_event, xwm);

    return wm;
}

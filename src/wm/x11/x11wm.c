/**
    Copyright (C) 2003-2004  Michael Ahlberg, Måns Rullgård

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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <pthread.h>
#include <semaphore.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <wm_x11_tc2.h>

typedef struct x11_wm {
    Display *dpy;
    Window win, swin;
    int width, height;
    int vw, vh;
    float aspect;
    wm_update_t update;
    void *cbd;
    int mouse;
    pthread_t eth, cth;
    int color_key;
    int flags;
    eventq_t qs;
} x11_wm_t;

static void *
x11_event(void *p)
{
    x11_wm_t *xwm = p;
    int run = 1;

    while(run){
	XEvent xe, nxe;

	XNextEvent(xwm->dpy, &xe);

	switch(xe.type){
	case ConfigureNotify: {
	    XWindowAttributes xwa;
	    Window foo;
	    int x, y, w, h;
	    int ckx, cky;
	    float wa;

	    while(XCheckTypedWindowEvent(xwm->dpy, xwm->win,
					 ConfigureNotify, &nxe))
		xe = nxe;

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

	    xwm->width = w;
	    xwm->height = h;

	    XMoveResizeWindow(xwm->dpy, xwm->swin, ckx, cky, w, h);
	    xwm->update(xwm->cbd, WM_MOVE, x, y, w, h);
	    break;
	}
	case Expose:
	    while(XCheckTypedWindowEvent(xwm->dpy, xwm->win, Expose, &nxe))
		xe = nxe;
	case MapNotify: {
	    xwm->update(xwm->cbd, WM_SHOW, 0, 0, 0, 0);
	    break;
	}
	case UnmapNotify: {
	    xwm->update(xwm->cbd, WM_HIDE, 0, 0, 0, 0);
	    break;
	}
	case KeyPress: {
	    int key = XLookupKeysym(&xe.xkey, 0);

	    switch(key){
	    case XK_space:
		tcvp_event_send(xwm->qs, TCVP_PAUSE);
		break;
	    case XK_Up:
		tcvp_event_send(xwm->qs, TCVP_SEEK, 27 * 60000000LL,
				TCVP_SEEK_REL);
		break;
	    case XK_Down:
		tcvp_event_send(xwm->qs, TCVP_SEEK, -27 * 60000000LL,
				TCVP_SEEK_REL);
		break;
	    case XK_q:
		tcvp_event_send(xwm->qs, TCVP_CLOSE);
		break;
	    case XK_Escape:
		tcvp_event_send(xwm->qs, TCVP_KEY, "escape");
		break;
	    }
	    break;
	}
	case ButtonPress: {
	    int bx, by;
	    Window foo;

	    XTranslateCoordinates(xwm->dpy, xwm->win, xwm->swin,
				  xe.xbutton.x, xe.xbutton.y,
				  &bx, &by, &foo);
	    bx = bx * xwm->vw / xwm->width;
	    by = by * xwm->vh / xwm->height;
	    tcvp_event_send(xwm->qs, TCVP_BUTTON, xe.xbutton.button,
			    TCVP_BUTTON_PRESS, bx, by);
	    break;
	}
	case MotionNotify:
	    if(!xwm->mouse)
		XUndefineCursor(xwm->dpy, xwm->win);
	    xwm->mouse = 1;
	    break;
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
    xwm->mouse = -1;
    pthread_cancel(xwm->cth);

    pthread_join(xwm->eth, NULL);
    pthread_join(xwm->cth, NULL);

    XCloseDisplay(xwm->dpy);
    eventq_delete(xwm->qs);
    free(xwm);
    free(wm);

    return 0;
}

static void *
x11_hidecursor(void *p)
{
    x11_wm_t *xwm = p;
    Cursor crs;
    Pixmap pm;
    XColor black;
    char data[8] = { [0 ... 7] = 0 };
    int hidden = 0;
    struct timespec ts = { tcvp_wm_x11_conf_mouse_delay, 0}, ts1;

    XAllocNamedColor(xwm->dpy,
		     DefaultColormap(xwm->dpy, DefaultScreen(xwm->dpy)),
		     "black", &black, &black);	
    pm = XCreateBitmapFromData(xwm->dpy, xwm->win, data, 8, 8);    
    crs = XCreatePixmapCursor(xwm->dpy, pm, pm, &black, &black, 0, 0);
    XFreePixmap(xwm->dpy, pm);

    while(xwm->mouse >= 0){
	nanosleep(&ts, &ts1);
	if(!xwm->mouse){
	    if(!hidden){
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		XDefineCursor(xwm->dpy, xwm->win, crs);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		hidden = 1;
	    }
	} else if(xwm->mouse < 0){
	    break;
	} else {
	    hidden = 0;
	}
	xwm->mouse = 0;
    }

    return NULL;
}

static void
x11_fullscreen(x11_wm_t *xwm)
{
    XEvent xev;
    Atom xa_wm_state, xa_fs;

    xa_fs = XInternAtom(xwm->dpy, "_NET_WM_STATE_FULLSCREEN", False);
    xa_wm_state = XInternAtom(xwm->dpy, "_NET_WM_STATE", False);

    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = xwm->win;
    xev.xclient.message_type = xa_wm_state;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = 1;
    xev.xclient.data.l[1] = xa_fs;
    xev.xclient.data.l[2] = 0;

    XSendEvent(xwm->dpy, RootWindow(xwm->dpy, DefaultScreen(xwm->dpy)), False,
	       SubstructureNotifyMask, &xev);
}


extern window_manager_t *
x11_open(int width, int height, wm_update_t upd, void *cbd,
	 tcconf_section_t *cs, int flags)
{
    window_manager_t *wm;
    x11_wm_t *xwm;
    Display *dpy;
    Window win;
    char *display = NULL;
    char *qname, *qn;
    int fs = 0;

    if(cs){
	tcconf_getvalue(cs, "video/device", "%s", &display);
	tcconf_getvalue(cs, "video/fullscreen", "%i", &fs);
    }

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
    tcconf_getvalue(cs, "video/color_key", "%i", &xwm->color_key);
    tcconf_getvalue(cs, "video/width", "%i", &xwm->vw);
    tcconf_getvalue(cs, "video/height", "%i", &xwm->vh);

    xwm->swin = XCreateWindow(dpy, win, 0, 0, width, height, 0, CopyFromParent,
			      InputOutput, CopyFromParent, 0, NULL);
    XSetWindowBackground(dpy, xwm->swin, xwm->color_key);

    qname = tcvp_event_get_qname(cs);
    qn = alloca(strlen(qname)+8);
    sprintf(qn, "%s/control", qname);
    xwm->qs = eventq_new(NULL);
    eventq_attach(xwm->qs, qn, EVENTQ_SEND);
    free(qname);

    wm = malloc(sizeof(*wm));
    wm->close = x11_close;
    wm->private = xwm;

    XSelectInput(xwm->dpy, xwm->win,
		 StructureNotifyMask | KeyPressMask | ExposureMask |
		 ButtonPressMask | PointerMotionMask);
    XSelectInput(xwm->dpy, xwm->swin, ExposureMask);
    XMapWindow(xwm->dpy, xwm->win);
    XMapSubwindows(xwm->dpy, xwm->win);

    if(fs)
	x11_fullscreen(xwm);

    pthread_create(&xwm->eth, NULL, x11_event, xwm);
    pthread_create(&xwm->cth, NULL, x11_hidecursor, xwm);

    if(display)
	free(display);

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

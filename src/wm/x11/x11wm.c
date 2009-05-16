/**
    Copyright (C) 2003-2006  Michael Ahlberg, Måns Rullgård

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
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <pthread.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <wm_x11_tc2.h>

typedef struct x11_wm {
    char *dpyname;
    Display *dpy;
    Window win, swin;
    int width, height;
    int owidth, oheight;
    int vw, vh;
    int ww, wh;
    int dx, dy;
    float aspect;
    pthread_t eth, cth;
    int color_key;
    int flags;
    eventq_t qs, qst;
    int net_wm;
    Atom wm_state;
    Atom wm_fullscreen;
    int fullscreen;
    int root;
    int depth;
    int ourwin;
    int run_event;
    int run_mouse;
    pthread_mutex_t mouse_lock;
    pthread_cond_t mouse_cond;
    tcvp_module_t *mod;
} x11_wm_t;

#define WM_STATE_REMOVE 0
#define WM_STATE_ADD    1
#define WM_STATE_TOGGLE 2

static pthread_mutex_t x11_lock;
static x11_wm_t *x11_xwm;

static void
x11_fullscreen(x11_wm_t *xwm, int fs)
{
    if(fs == WM_STATE_TOGGLE)
	xwm->fullscreen ^= 1;
    else
	xwm->fullscreen = fs;

    if(xwm->net_wm){
	XEvent xev;

	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.window = xwm->win;
	xev.xclient.message_type = xwm->wm_state;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = xwm->fullscreen;
	xev.xclient.data.l[1] = xwm->wm_fullscreen;
	xev.xclient.data.l[2] = 0;

	XSendEvent(xwm->dpy, RootWindow(xwm->dpy, DefaultScreen(xwm->dpy)),
		   False, SubstructureNotifyMask, &xev);
    } else {
	if(xwm->fullscreen){
	    XWindowAttributes xwa;
	    XGetWindowAttributes(xwm->dpy, DefaultRootWindow(xwm->dpy), &xwa);
	    XMoveResizeWindow(xwm->dpy, xwm->win, 0, 0, xwa.width, xwa.height);
	} else {
	    XResizeWindow(xwm->dpy, xwm->win, xwm->owidth, xwm->oheight);
	}
    }
}

extern int
x11_ev_fullscreen(tcvp_module_t *m, tcvp_event_t *te)
{
    x11_wm_t *xwm = m->private;
    tcvp_wm_fullscreen_event_t *fse = (tcvp_wm_fullscreen_event_t *) te;
    x11_fullscreen(xwm, fse->op);
    return 0;
}

extern int
x11_ev_restore(tcvp_module_t *m, tcvp_event_t *te)
{
    x11_wm_t *xwm = m->private;
    x11_fullscreen(xwm, WM_STATE_REMOVE);
    XResizeWindow(xwm->dpy, xwm->win, xwm->owidth, xwm->oheight);
    return 0;
}

static void
x11_key_event(x11_wm_t *xwm, XKeyEvent *k)
{
    KeySym key;
    int len;
    char buf[16];
    char *ks = NULL;

    len = XLookupString(k, buf, sizeof(buf), &key, NULL);
    if(key != NoSymbol)
	ks = XKeysymToString(key);

    if(ks)
	tcvp_event_send(xwm->qs, TCVP_KEY, ks);
}

static void
x11_update_win(x11_wm_t *xwm)
{
    XWindowAttributes xwa;
    int x, y, w, h;
    float wa;

    XGetWindowAttributes(xwm->dpy, xwm->win, &xwa);
    xwm->ww = xwa.width;
    xwm->wh = xwa.height;
    xwm->depth = xwa.depth;

    wa = (float) xwa.width / xwa.height;

    if(wa > xwm->aspect){
	h = xwa.height;
	w = h * xwm->aspect;
	x = (xwa.width - w) / 2;
	y = 0;
    } else {
	w = xwa.width;
	h = w / xwm->aspect;
	y = (xwa.height - h) / 2;
	x = 0;
    }

    xwm->dx = x;
    xwm->dy = y;
    xwm->width = w;
    xwm->height = h;
}

static void
x11_pixmap_bg(x11_wm_t *xwm)
{
    Pixmap p;
    GC gc;

    p = XCreatePixmap(xwm->dpy, xwm->win, xwm->ww, xwm->wh, xwm->depth);
    gc = XCreateGC(xwm->dpy, p, 0, NULL);
    XSetForeground(xwm->dpy, gc, BlackPixel(xwm->dpy,DefaultScreen(xwm->dpy)));
    XFillRectangle(xwm->dpy, p, gc, 0, 0, xwm->ww, xwm->wh);
    XSetForeground(xwm->dpy, gc, xwm->color_key);
    XFillRectangle(xwm->dpy, p, gc, xwm->dx, xwm->dy, xwm->width, xwm->height);
    XSetWindowBackgroundPixmap(xwm->dpy, xwm->win, p);
    XFreeGC(xwm->dpy, gc);
    XFreePixmap(xwm->dpy, p);
    XClearWindow(xwm->dpy , xwm->win);
}

static void *
x11_event(void *p)
{
    x11_wm_t *xwm = p;

    tc2_print("X11", TC2_PRINT_DEBUG, "X event listener starting\n");

    while(xwm->run_event){
	XEvent xe, nxe;
	int x, y;

	XNextEvent(xwm->dpy, &xe);
	if(!xwm->run_event)
	    break;

	switch(xe.type){
	case ConfigureNotify: {
	    if(xe.xany.window != xwm->win)
		break;

	    while(XCheckTypedWindowEvent(xwm->dpy, xwm->win,
					 ConfigureNotify, &nxe))
		xe = nxe;

	    x11_update_win(xwm);

	    if(xwm->flags & WM_ABSCOORD){
		Window foo;
		XTranslateCoordinates(xwm->dpy, xwm->win,
				      DefaultRootWindow(xwm->dpy), 0, 0,
				      &x, &y, &foo);
	    } else {
		x = 0;
		y = 0;
	    }

	    if(xwm->swin != None){
		XMoveResizeWindow(xwm->dpy, xwm->swin, xwm->dx, xwm->dy,
				  xwm->width, xwm->height);
	    } else {
		x11_pixmap_bg(xwm);
		x = xwm->dx;
		y = xwm->dy;
	    }

            tcvp_event_send(xwm->qst, TCVP_WM_MOVE, x, y, xwm->width,
                            xwm->height);
	    break;
	}
	case Expose:
	    if(xe.xany.window != (xwm->swin != None? xwm->swin: xwm->win))
		break;
	    while(XCheckTypedWindowEvent(xwm->dpy, xwm->win, Expose, &nxe))
		xe = nxe;
	case MapNotify: {
            tcvp_event_send(xwm->qst, TCVP_WM_SHOW);
	    break;
	}
	case UnmapNotify: {
            tcvp_event_send(xwm->qst, TCVP_WM_HIDE);
	    break;
	}
	case KeyPress: {
	    x11_key_event(xwm, &xe.xkey);
	    break;
	}
	case ButtonPress: {
	    int bx, by;
	    Window foo;

	    if(xwm->swin != None){
		XTranslateCoordinates(xwm->dpy, xwm->win, xwm->swin,
				      xe.xbutton.x, xe.xbutton.y,
				      &bx, &by, &foo);
	    } else {
		bx = xe.xbutton.x - xwm->dx;
		by = xe.xbutton.y - xwm->dy;
	    }

	    bx = bx * xwm->vw / xwm->width;
	    by = by * xwm->vh / xwm->height;
	    tcvp_event_send(xwm->qs, TCVP_BUTTON, xe.xbutton.button,
			    TCVP_BUTTON_PRESS, bx, by);
	    break;
	}
	case MotionNotify:
	    pthread_mutex_lock(&xwm->mouse_lock);
	    pthread_cond_broadcast(&xwm->mouse_cond);
	    pthread_mutex_unlock(&xwm->mouse_lock);
	    break;
	case DestroyNotify:
	    tc2_print("X11", TC2_PRINT_DEBUG, "DestroyNotify\n");
	    xwm->run_event = 0;
	    break;

	}
    }

    tc2_print("X11", TC2_PRINT_DEBUG, "X event listener done\n");

    return NULL;
}

static void *
x11_hidecursor(void *p)
{
    x11_wm_t *xwm = p;
    Cursor crs;
    Pixmap pm;
    XColor black;
    char data[8] = { [0 ... 7] = 0 };
    int hide = 0, hidden = 0;
    struct timespec ts;
    struct timeval stime;

    tc2_print("X11", TC2_PRINT_DEBUG, "cursor hider starting\n");

    XAllocNamedColor(xwm->dpy,
		     DefaultColormap(xwm->dpy, DefaultScreen(xwm->dpy)),
		     "black", &black, &black);
    pm = XCreateBitmapFromData(xwm->dpy, xwm->win, data, 8, 8);
    crs = XCreatePixmapCursor(xwm->dpy, pm, pm, &black, &black, 0, 0);
    XFreePixmap(xwm->dpy, pm);

    pthread_mutex_lock(&xwm->mouse_lock);

    while(xwm->run_mouse){
	if(!hidden){
	    gettimeofday(&stime, NULL);
	    ts.tv_sec = stime.tv_sec + tcvp_wm_x11_conf_mouse_delay;
	    ts.tv_nsec = stime.tv_usec * 1000;
	    hide = pthread_cond_timedwait(&xwm->mouse_cond, &xwm->mouse_lock,
					  &ts);
	} else {
	    pthread_cond_wait(&xwm->mouse_cond, &xwm->mouse_lock);
	    hide = 0;
	}

	if(!xwm->run_mouse)
	    break;

	pthread_mutex_unlock(&xwm->mouse_lock);

	if(hide != hidden){
	    if(hide){
		tc2_print("X11", TC2_PRINT_DEBUG, "hiding cursor\n");
		XDefineCursor(xwm->dpy, xwm->win, crs);
	    } else {
		tc2_print("X11", TC2_PRINT_DEBUG, "unhiding cursor\n");
		XUndefineCursor(xwm->dpy, xwm->win);
	    }
	    XSync(xwm->dpy, False);
	    hidden = hide;
	}

	pthread_mutex_lock(&xwm->mouse_lock);
    }

    pthread_mutex_unlock(&xwm->mouse_lock);

    XFreeCursor(xwm->dpy, crs);

    tc2_print("X11", TC2_PRINT_DEBUG, "cursor hider done\n");

    return NULL;
}

static int
x11_close(window_manager_t *wm)
{
    x11_wm_t *xwm = wm->private;

    if(tcvp_wm_x11_conf_reuse_window){
	pthread_mutex_lock(&x11_lock);
	if(!x11_xwm){
#if 0
	    if(!xwm->root)
		XSelectInput(xwm->dpy, xwm->win, KeyPressMask |
			     ButtonPressMask | PointerMotionMask);
	    else
		XSelectInput(xwm->dpy, xwm->win, 0);
	    if(xwm->swin != None)
		XSelectInput(xwm->dpy, xwm->swin, 0);
#endif
	    x11_xwm = tcref(xwm);
	}
	pthread_mutex_unlock(&x11_lock);
    }

    tcfree(xwm);
    free(wm);

    return 0;
}

static void
x11_freewm(void *p)
{
    x11_wm_t *xwm = p;

    xwm->mod->private = NULL;
    tcfree(xwm->mod);

    if(xwm->root){
	Atom xa_rootpmap = XInternAtom(xwm->dpy, "_XROOTPMAP_ID", True);

	if(xa_rootpmap != None){
	    Atom xa_pmap = XInternAtom(xwm->dpy, "PIXMAP", True);
	    Atom aret;
	    int fret;
	    unsigned long nitems = 0, remain;
	    unsigned char *buf;

	    XGetWindowProperty(xwm->dpy, xwm->win, xa_rootpmap, 0, 1, False,
			       xa_pmap, &aret, &fret, &nitems, &remain, &buf);
	    if(nitems > 0){
		Pixmap rpm = *((Pixmap*)buf);
		XSetWindowBackgroundPixmap(xwm->dpy, xwm->win, rpm);
		XFree(buf);
	    }
	}
    }

    tc2_print("X11", TC2_PRINT_DEBUG, "stopping cursor hider\n");

    pthread_mutex_lock(&xwm->mouse_lock);
    xwm->run_mouse = 0;
    pthread_cond_broadcast(&xwm->mouse_cond);
    pthread_mutex_unlock(&xwm->mouse_lock);
    pthread_join(xwm->cth, NULL);

    if(xwm->swin == None){
	xwm->run_event = 0;
	XClearArea(xwm->dpy, xwm->win, 0, 0, 0, 0, True);
    } else {
	if(xwm->ourwin)
	    XDestroyWindow(xwm->dpy, xwm->win);
	else
	    XDestroyWindow(xwm->dpy, xwm->swin);
    }

    XSync(xwm->dpy, False);

    tc2_print("X11", TC2_PRINT_DEBUG, "waiting for event listener\n");
    pthread_join(xwm->eth, NULL);

    XCloseDisplay(xwm->dpy);
    free(xwm->dpyname);
    eventq_delete(xwm->qs);
    eventq_delete(xwm->qst);
}

static int
check_wm(x11_wm_t *xwm)
{
    Atom supporting, xa_window;
    Atom r_type;
    int ret = 0;

    xa_window = XInternAtom(xwm->dpy, "WINDOW", True);
    supporting = XInternAtom(xwm->dpy, "_NET_SUPPORTING_WM_CHECK", True);

    if(supporting != None){
	unsigned long count, bytes_remain;
	unsigned char *p = NULL, *p2 = NULL;
	int r, r_format;

	r = XGetWindowProperty(xwm->dpy, DefaultRootWindow(xwm->dpy),
			       supporting, 0, 1, False, xa_window,
			       &r_type, &r_format, &count, &bytes_remain, &p);

	if(r == Success && p && r_type == xa_window && r_format == 32 &&
	   count == 1){
	    Window w = *(Window *)p;

	    r = XGetWindowProperty(xwm->dpy, w, supporting, 0, 1,
				   False, xa_window, &r_type, &r_format,
				   &count, &bytes_remain, &p2);

	    if(r == Success && p2 && *p2 == *p && r_type == xa_window &&
	       r_format == 32 && count == 1){
		ret = 1;
	    }
	}

	if(p)
	    XFree(p);
	if(p2)
	    XFree(p2);
    }

    xwm->net_wm = ret;
    return ret;
}

extern window_manager_t *
x11_open(int width, int height, tcconf_section_t *cs, int flags)
{
    window_manager_t *wm = NULL;
    x11_wm_t *xwm = NULL;
    char *display = NULL;
    int fs = tcvp_wm_x11_conf_fullscreen;
    char *window = NULL;
    int subwin = tcvp_wm_x11_conf_subwindow;

    if(cs){
	tcconf_getvalue(cs, "video/device", "%s", &display);
	tcconf_getvalue(cs, "video/fullscreen", "%i", &fs);
	tcconf_getvalue(cs, "video/window", "%s", &window);
    }

    XInitThreads();

    if(tcvp_wm_x11_conf_reuse_window && !window){
	pthread_mutex_lock(&x11_lock);
	if(x11_xwm && ((!x11_xwm->dpyname && !display) ||
		       !strcmp(x11_xwm->dpyname, display))){
	    xwm = x11_xwm;
	    x11_xwm = NULL;
	}
	pthread_mutex_unlock(&x11_lock);
    }

    if(!xwm){
	Display *dpy = XOpenDisplay(display);
	if(!dpy){
	    tc2_print("X11WM", TC2_PRINT_ERROR, "can't open display %s\n",
		      display);
	    goto out;
	}

	xwm = tcallocdz(sizeof(*xwm), NULL, x11_freewm);
	xwm->dpyname = display? strdup(display): display;
	xwm->dpy = dpy;
        xwm->qs = tcvp_event_get_sendq(cs, "control");
        xwm->qst = tcvp_event_get_sendq(cs, "status");

	check_wm(xwm);
	xwm->wm_state = XInternAtom(xwm->dpy, "_NET_WM_STATE", False);
	xwm->wm_fullscreen =
	    XInternAtom(xwm->dpy, "_NET_WM_STATE_FULLSCREEN", False);

        xwm->mod = wm_x11_new(cs);
        xwm->mod->private = xwm;
        xwm->mod->init(xwm->mod);
    }


    xwm->width = width;
    xwm->height = height;
    xwm->owidth = width;
    xwm->oheight = height;
    xwm->aspect = (float) width / height;
    xwm->flags = flags;
    tcconf_getvalue(cs, "video/color_key", "%i", &xwm->color_key);
    tcconf_getvalue(cs, "video/width", "%i", &xwm->vw);
    tcconf_getvalue(cs, "video/height", "%i", &xwm->vh);

    if(window){
	if(!strcmp(window, "root"))
	    xwm->win = DefaultRootWindow(xwm->dpy);
	else
	    xwm->win = strtoul(window, NULL, 0);
	if(xwm->win == DefaultRootWindow(xwm->dpy)){
	    xwm->root = 1;
	    fs = 1;
	    subwin = 0;
	}
    } else {
	if(xwm->win != None){
	    if(!fs){
		XResizeWindow(xwm->dpy, xwm->win, width, height);
		XSync(xwm->dpy, False);
	    }
	} else {
	    xwm->win = XCreateWindow(xwm->dpy, DefaultRootWindow(xwm->dpy),
				     0, 0, width, height, 0, CopyFromParent,
				     InputOutput, CopyFromParent, 0, NULL);
	    XSetWindowBackground(xwm->dpy, xwm->win, 0);
	}
	xwm->ww = width;
	xwm->wh = height;
	xwm->ourwin = 1;
    }

    x11_update_win(xwm);

    if(!subwin){
	xwm->swin = None;
	x11_pixmap_bg(xwm);
    } else {
	if(xwm->swin == None){
	    xwm->swin = XCreateWindow(xwm->dpy, xwm->win, xwm->dx, xwm->dy,
				      xwm->width, xwm->height, 0,
				      CopyFromParent, InputOutput,
				      CopyFromParent, 0, NULL);
	    XSetWindowBackground(xwm->dpy, xwm->swin, xwm->color_key);
	} else {
	    XMoveResizeWindow(xwm->dpy, xwm->swin, xwm->dx, xwm->dy,
			      xwm->width, xwm->height);
	}
    }

    if(xwm->swin != None)
        tcvp_event_send(xwm->qst, TCVP_WM_MOVE, 0, 0, xwm->width, xwm->height);
    else
        tcvp_event_send(xwm->qst, TCVP_WM_MOVE, xwm->dx, xwm->dy,
                        xwm->width, xwm->height);

    wm = malloc(sizeof(*wm));
    wm->close = x11_close;
    wm->private = xwm;

    if(!xwm->root)
	XSelectInput(xwm->dpy, xwm->win,
		     StructureNotifyMask | KeyPressMask | ExposureMask |
		     ButtonPressMask | PointerMotionMask);
    else
	XSelectInput(xwm->dpy, xwm->win, ExposureMask | PointerMotionMask);

    if(xwm->swin != None)
	XSelectInput(xwm->dpy, xwm->swin, ExposureMask | StructureNotifyMask);

    if(!xwm->root)
	XMapWindow(xwm->dpy, xwm->win);
    if(xwm->swin != None)
	XMapWindow(xwm->dpy, xwm->swin);

    if(fs)
	x11_fullscreen(xwm, WM_STATE_ADD);

    if(!xwm->run_event){
	xwm->run_event = 1;
	pthread_create(&xwm->eth, NULL, x11_event, xwm);
    }

    if(!xwm->run_mouse){
	xwm->run_mouse = 1;
	pthread_mutex_init(&xwm->mouse_lock, NULL);
	pthread_cond_init(&xwm->mouse_cond, NULL);
	pthread_create(&xwm->cth, NULL, x11_hidecursor, xwm);
    }

out:
    if(display)
	free(display);
    if(window)
	free(window);

    return wm;
}

extern int
x11_getwindow(window_manager_t *wm, Display **dpy, Window *win)
{
    x11_wm_t *xwm = wm->private;
    *dpy = xwm->dpy;
    *win = xwm->swin != None? xwm->swin: xwm->win;
    return 0;
}

extern int
x11_init(char *p)
{
    pthread_mutex_init(&x11_lock, NULL);
    return 0;
}

extern int
x11_free(void)
{
    pthread_mutex_destroy(&x11_lock);
    tcfree(x11_xwm);
    return 0;
}

/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include "widgets.h"
#include <string.h>

#define MWM_HINTS_DECORATIONS   (1L << 1)
#define PROP_MWM_HINTS_ELEMENTS 5
typedef struct _mwmhints {
    uint32_t flags;
    uint32_t functions;
    uint32_t decorations;
    int32_t  input_mode;
    uint32_t status;
} MWMHints;

#define _NET_WM_STATE_REMOVE        0
#define _NET_WM_STATE_ADD           1
#define _NET_WM_STATE_TOGGLE        2

int mapped = 1;
int quit = 0;
static pthread_t xth, sth;
int (*dnd_cb)(char *);

Atom XdndEnter, XdndLeave, XdndPosition, XdndStatus, XdndDrop,
    XdndType, XdndSelection, XdndFinished, XdndActionPrivate, tcvpxa;

Display *xd;
int xs;
GC wgc, bgc;
Pixmap root;
int root_width;
int root_height;
int depth;
tcwidget_t *drag, *pbt, *cbt, *tcbt;

extern void *
x11_event(void *p)
{
    while(!quit){
        XEvent xe;

        XNextEvent(xd, &xe);
/* 	if(xe.type != 14 && xe.type != 12 && xe.type != 22) */
/* 	    fprintf(stderr, "%d\n", xe.type); */
        switch(xe.type){
	case Expose:
	    draw_widgets();
	    break;

	case ConfigureNotify:
	{
	    list_item *current=NULL;
	    window_t *s;

	    while((s = list_next(window_list, &current))!=NULL) {
		if(xe.xconfigure.window == s->xw){
		    if(s->background->transparent) {
			if(!drag) {
			    repaint_window(s);
			    draw_window(s);
			}
		    }
		    s->x = xe.xconfigure.x;
		    s->y = xe.xconfigure.y;
		    break;
		}
	    }
	    break;
	}

 	case ButtonRelease:
	{
	    list_item *current=NULL;
	    tcwidget_t *w;

	    if(drag){
		if(drag->common.drag_end){
		    drag->common.drag_end((xtk_widget_t *) drag, &xe);
		}
		drag = NULL;
	    }
	    if(pbt) {
		if(pbt->common.release){
		    pbt->common.release((xtk_widget_t *) pbt, &xe);
		}
		pbt = NULL;
	    }

	    if(cbt) {
		while((w = list_next(click_list, &current))!=NULL) {
		    if(xe.xbutton.window == w->common.win){
			if(cbt == w) {
			    cbt->common.onclick((xtk_widget_t *) w, &xe);
			    cbt = NULL;
			}
		    }
		}
	    }
	    break;
	}

 	case MotionNotify:
	{
	    if(drag){
		drag->common.ondrag((xtk_widget_t *) drag, &xe);
	    }
	    break;
	}

 	case ButtonPress:	
	    if(xe.xbutton.button == 1) {
		list_item *current=NULL;
		tcwidget_t *w;

		while((w = list_next(click_list, &current))!=NULL) {
		    if(xe.xbutton.window == w->common.win){
			if(w->common.enabled && w->common.onpress){
			    w->common.onpress((xtk_widget_t *) w, &xe);
			}
			if(w->common.enabled && w->common.onclick){
			    cbt = w;
			}
			if(w->common.enabled && w->common.press){
			    w->common.press((xtk_widget_t *) w, &xe);
			    pbt = w;
			}
			if(w->common.enabled && w->common.ondrag) {
			    drag = w;
			    if(w->common.drag_begin){
				w->common.drag_begin((xtk_widget_t *) w, &xe);
			    }
			}
		    }
		}
	    }
	    break;

 	case EnterNotify:
	{
	    list_item *current=NULL;
	    tcwidget_t *w;

	    while((w = list_next(click_list, &current))!=NULL) {
		if(xe.xbutton.window == w->common.win){
		    if(w == tcbt) {
			cbt = tcbt;
			tcbt = NULL;
		    }
		    if(w->common.enter){
			w->common.enter((xtk_widget_t *) w, &xe);
		    }
		}
	    }

	    break;
	}

	case LeaveNotify:
	{
	    list_item *current=NULL;
	    tcwidget_t *w;

	    if(cbt) {
		tcbt = cbt;
		cbt = NULL;
	    }
	    while((w = list_next(click_list, &current))!=NULL) {
		if(xe.xbutton.window == w->common.win){
		    if(w->common.exit){
			w->common.exit((xtk_widget_t *) w, &xe);
		    }
		}
	    }

	    break;
	}

	case MapNotify:	    
	    mapped = 1;
	    repaint_widgets();
	    break;

	case UnmapNotify:
	    mapped = 0;
	    break;

	case SelectionNotify:
	{
	    Atom ret_type;
	    int ret_format;
	    unsigned long ret_item;
	    unsigned long remain_byte;
	    unsigned char *foo;
	    char *buf, *tmp;

	    XEvent xevent;
	    Window selowner = XGetSelectionOwner(xd, XdndSelection);

	    XGetWindowProperty(xd, xe.xselection.requestor,
			       tcvpxa, 0, 65536, True, XdndType,
			       &ret_type, &ret_format,
			       &ret_item, &remain_byte,
			       &foo);

	    if(!foo)
		break;

	    buf = strdup(foo);
	    XFree(foo);
	    foo = buf;

	    memset (&xevent, 0, sizeof(xevent));
	    xevent.xany.type = ClientMessage;
	    xevent.xany.display = xd;
	    xevent.xclient.window = selowner;
	    xevent.xclient.message_type = XdndFinished;
	    xevent.xclient.format = 32;
	    xevent.xclient.data.l[0] = xe.xselection.requestor;
	    XSendEvent(xd, selowner, 0, 0, &xevent);

	    if(dnd_cb) {
		do {
		    if((tmp = strchr(buf, '\n')))
			*tmp++ = 0;
		    dnd_cb(buf);
		    buf = tmp;
		} while(tmp);
	    }

	    free(foo);
	    break;
	}

	case ClientMessage:
	    if(xe.xclient.message_type == XdndEnter) {
		/* Nothing yet */
	    } else if(xe.xclient.message_type == XdndPosition) {
		XEvent xevent;

		memset (&xevent, 0, sizeof(xevent));
		xevent.xany.type = ClientMessage;
		xevent.xany.display = xd;
		xevent.xclient.window = xe.xclient.data.l[0];
		xevent.xclient.message_type = XdndStatus;
		xevent.xclient.format = 32;

		xevent.xclient.data.l[0] = xe.xclient.window;
		xevent.xclient.data.l[1] = 3;
		xevent.xclient.data.l[2] = 0;
		xevent.xclient.data.l[3] = 0;
		xevent.xclient.data.l[4] = XdndActionPrivate;

		XSendEvent(xd, xe.xclient.data.l[0], 0, 0, &xevent);

	    } else if(xe.xclient.message_type == XdndDrop) {
		XConvertSelection(xd, XdndSelection, XdndType, tcvpxa,
				  xe.xclient.window, CurrentTime);

	    } else if(xe.xclient.message_type == XdndLeave) {
		/* Nothing yet */
	    }
	    break;

	default:
	    break;
	}
    }
    return NULL;
}

static int
check_wm(window_t *window)
{
    Atom supporting, xa_window;
    Atom r_type;
    int r_format, r, ret = 0;
    unsigned long count, bytes_remain;
    unsigned char *p = NULL, *p2 = NULL;
    

    xa_window = XInternAtom(xd, "WINDOW", True);
    supporting = XInternAtom(xd, "_NET_SUPPORTING_WM_CHECK", True);

    if(supporting != None) {
	r = XGetWindowProperty(xd, RootWindow(xd, xs), supporting,
			       0, 1, False, xa_window, &r_type, &r_format,
			       &count, &bytes_remain, &p);

	if(r == Success && p && r_type == xa_window && r_format == 32 &&
	   count == 1) {
	    Window w = *(Window *)p;

	    r = XGetWindowProperty(xd, w, supporting, 0, 1,
				   False, xa_window, &r_type, &r_format,
				   &count, &bytes_remain, &p2);
	
	    if (r == Success && p2 && *p2 == *p &&
		r_type == xa_window && r_format == 32 && count == 1) {
		ret = 1;
	    }
	}

	if (p) {
	    XFree(p);

	    if (p2) {
		XFree(p2);
	    }
	}
    }

    window->net_wm_support = ret;
    return ret;
}


static int
wm_set_property(window_t *window, char *atom, int enabled)
{
    XEvent xev;
    Atom xa_wm_state, xa_prop;

    xa_prop = XInternAtom(xd, atom, False);
    xa_wm_state = XInternAtom(xd, "_NET_WM_STATE", False);

    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = window->xw;
    xev.xclient.message_type = xa_wm_state;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = (enabled>0)?_NET_WM_STATE_ADD:_NET_WM_STATE_REMOVE;
    xev.xclient.data.l[1] = xa_prop;
    xev.xclient.data.l[2] = 0;

    XSendEvent(xd, RootWindow(xd, xs), False,
	       SubstructureNotifyMask, (XEvent *) & xev);

    return 0;
}


extern int
set_sticky(window_t *window, int enabled)
{
    if(window->net_wm_support != 0) {
	Atom xa_wm_desktop;
	XEvent xev;

	long desktop = 0xffffffffUL;
	Atom xa_cardinal, xa_current_desktop;
	Atom r_type;
	int r_format, ret;
	unsigned long count, bytes_remain;
	unsigned char *p = NULL;

	wm_set_property(window, "_NET_WM_STATE_STICKY", enabled?1:0);

	if(enabled <= 0) {
	    xa_cardinal = XInternAtom(xd, "CARDINAL", True);
	    xa_current_desktop = XInternAtom(xd, "_NET_CURRENT_DESKTOP", True);

	    ret = XGetWindowProperty(xd, RootWindow(xd, xs),
				     xa_current_desktop, 0, 1, False,
				     xa_cardinal, &r_type, &r_format,
				     &count, &bytes_remain, &p);

	    if(ret == Success && p && r_type == xa_cardinal &&
	       r_format == 32 && count == 1) {
		desktop = *(long *)p;
	    } else {
		desktop = 0;
	    }
	}

	xa_wm_desktop = XInternAtom(xd, "_NET_WM_DESKTOP", False);

	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.window = window->xw;
	xev.xclient.message_type = xa_wm_desktop;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = desktop;

	XSendEvent(xd, RootWindow(xd, xs), False,
		   SubstructureNotifyMask, &xev);
    }

    return 0;
}


extern int
set_always_on_top(window_t *window, int enabled)
{
    if(window->net_wm_support != 0) {
	wm_set_property(window, "_NET_WM_STATE_STAYS_ON_TOP", enabled?1:0);
    }

    return 0;
}


extern int
set_dnd_cb(int(*cb)(char *))
{
    dnd_cb = cb;
    return 0;
}


extern int
init_graphics()
{
    click_list = list_new(TC_LOCK_SLOPPY);
    widget_list = list_new(TC_LOCK_SLOPPY);
    sl_list = list_new(TC_LOCK_SLOPPY);
    window_list = list_new(TC_LOCK_SLOPPY);

    XInitThreads();
    xd = XOpenDisplay(NULL);

    XSetCloseDownMode (xd, DestroyAll);
    xs = DefaultScreen (xd);

    root_width = XDisplayWidth(xd,xs);
    root_height = XDisplayHeight(xd,xs);
    depth = DefaultDepth(xd, xs);

    root = XCreatePixmap(xd, RootWindow(xd, xs), root_width,
			 root_height, depth);

    pthread_create(&xth, NULL, x11_event, NULL);
    pthread_create(&sth, NULL, scroll_labels, NULL);

    return 0;
}


extern int
shdn_graphics()
{
    quit = 1;

    /* Get some events, should be done in a better way */
    Window xw = XCreateWindow(xd, RootWindow(xd, xs), 0, 0,
			      1, 1, 0, CopyFromParent, InputOutput,
			      CopyFromParent, 0, 0);
    XSelectInput(xd, xw, StructureNotifyMask);
    XDestroyWindow(xd, xw);
    XSync(xd, False);

    pthread_join(xth, NULL);
    pthread_join(sth, NULL);

    XCloseDisplay(xd);

    list_destroy(click_list, NULL);
    list_destroy(widget_list, NULL);
    list_destroy(sl_list, NULL);
    list_destroy(window_list, NULL);

    return 0;
}


extern window_t *
create_window(char *title, int width, int height)
{
    Atom prop;
    MWMHints mwmhints;
    XClassHint *classhints;
    XSizeHints *sizehints;
    XTextProperty windowName;
    Atom xdndversion = 3, xa_atom;
    Window toplevel;

    window_t *window = calloc(sizeof(window_t), 1);
    window->width = width;
    window->height = height;

    window->xw = XCreateWindow(xd, RootWindow(xd, xs), 0, 0,
			     window->width, window->height, 0,
			     CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);

    XSelectInput(xd, window->xw, ExposureMask | StructureNotifyMask |
		 EnterWindowMask | LeaveWindowMask | ButtonPressMask |
	         ButtonReleaseMask | PointerMotionMask);

    xa_atom = XInternAtom(xd, "ATOM", False);

    XdndDrop = XInternAtom(xd, "XdndDrop", False);
    XdndPosition = XInternAtom(xd, "XdndPosition", False);
    XdndStatus = XInternAtom(xd, "XdndStatus", False);
    XdndEnter = XInternAtom(xd, "XdndEnter", False);
    XdndLeave = XInternAtom(xd, "XdndLeave", False);
    XdndType = XInternAtom(xd, "text/plain", False);
    XdndFinished = XInternAtom(xd, "XdndFinished", False);
    XdndSelection = XInternAtom(xd, "XdndSelection", False);
    XdndActionPrivate = XInternAtom(xd, "XdndActionPrivate", False);
    tcvpxa = XInternAtom(xd, "TCVPSEL", False);

    memset(&mwmhints, 0, sizeof(MWMHints));
    prop = XInternAtom(xd, "_MOTIF_WM_HINTS", False);
    mwmhints.flags = MWM_HINTS_DECORATIONS;
    mwmhints.decorations = 0;
    XChangeProperty(xd, window->xw, prop, prop, 32, PropModeReplace,
		    (unsigned char *) &mwmhints,
		    PROP_MWM_HINTS_ELEMENTS);

    check_wm(window);

    prop = XInternAtom(xd, "XdndAware", False);
    toplevel = window->xw;
    for(;;) {
	Window root, w, *c;
	int n;
	XQueryTree(xd, toplevel, &root, &w, &c, &n);
	XFree(c);
	if(w != root) {
	    toplevel = w;
	} else {
	    break;
	}
    }
    XChangeProperty(xd, toplevel, prop, xa_atom, 32,
		    PropModeReplace, (unsigned char *) &xdndversion, 1);

    window->bgc = XCreateGC (xd, window->xw, 0, NULL);
    XSetBackground(xd, window->bgc, 0x00000000);
    XSetForeground(xd, window->bgc, 0xFFFFFFFF);

    window->wgc = XCreateGC (xd, window->xw, 0, NULL);
    XSetBackground(xd, window->bgc, 0xFFFFFFFF);
    XSetForeground(xd, window->bgc, 0x00000000);

    classhints = XAllocClassHint ();
    classhints->res_name = "TCVP";
    classhints->res_class = "TCVP";

    sizehints = XAllocSizeHints ();
    sizehints->flags = PSize | PMinSize | PMaxSize;
    sizehints->min_width = window->width;
    sizehints->min_height = window->height;
    sizehints->max_width = window->width;
    sizehints->max_height = window->height;

    XStringListToTextProperty (&title, 1, &windowName);
    XSetWMProperties (xd, window->xw, &windowName, NULL, NULL,
		      0, sizehints, NULL, classhints);
    XFree(windowName.value);
    XFree(sizehints);
    XFree(classhints);

    XSync(xd, False);

    window->widgets = list_new(TC_LOCK_SLOPPY);

    list_push(window_list, window);

    window->enabled = 1;
    clear_shape(window->xw);

    return window;
}


extern int
show_window(window_t *window)
{
    tcwidget_t *w;
    list_item *current=NULL;

    if(!window->subwindow) {
	XMapWindow (xd, window->xw);
    }
    window->mapped = 1;

    while((w = list_next(window->widgets, &current))!=NULL) {
/* 	if(w->common.visible == 1) { */
/* 	        XMapWindow(xd, w->common.win); */
/* 	} */
	if(w->type == TCBOX && w->box.visible != 0) {
	    show_window(w->box.subwindow);
	}
    }

    return 0;
}


extern int
hide_window(window_t *window)
{
    tcwidget_t *w;
    list_item *current=NULL;

    if(!window->subwindow) {
	XUnmapWindow (xd, window->xw);
    }
/*     XUnmapSubwindows(xd, window->xw); */
    window->mapped = 0;

    while((w = list_next(window->widgets, &current))!=NULL) {
	if(w->type == TCBOX && w->box.visible != 0) {
	    hide_window(w->box.subwindow);
	}
    }

    return 0;
}


extern int 
destroy_window(window_t *window)
{
    tcwidget_t *w;

    window->enabled = 0;
    list_delete(window_list, window, widget_cmp, NULL);

    while((w = list_pop(window->widgets))!=NULL) {
	if(w->type == TCBOX) {
	    destroy_window(w->box.subwindow);
	}
	destroy_widget(w);
    }

    list_destroy(window->widgets, NULL);
    if(!window->subwindow){
	XDestroyWindow(xd, window->xw);
    }
    if(window->background) {
	free(window->background);
    }
    XFreeGC(xd, window->bgc);
    XFreeGC(xd, window->wgc);
    free(window);

    return 0;
}

extern xtk_position_t*
get_win_pos(window_t *win)
{
    xtk_position_t *pos = malloc(sizeof(*pos));
    pos->x = win->x;
    pos->y = win->y;
    return pos;
}

extern int
set_win_pos(window_t *win, xtk_position_t *pos)
{
    XMoveWindow(xd, win->xw, pos->x, pos->y);
    return 0;
}


extern xtk_size_t*
get_screen_size()
{
    xtk_size_t *s = malloc(sizeof(*s));
    s->w = root_width;
    s->h = root_height;
    return s;
}


extern int
clear_shape(Window w)
{
    XShapeCombineShape(xd, w, ShapeBounding, 0, 0, w,
		       ShapeBounding, ShapeSubtract);
    return 0;
}

extern int
shape_window(Window w, image_info_t *im, int op, Pixmap *shape)
{
    int transparent = 0;
    Pixmap maskp;
    char *data;
    int x, y;
    int w8;

    w8 = ((im->width + 7) & ~7) / 8;
    data = calloc(1, im->width * im->height);
    for(y = 0; y < im->height; y++){
	for(x = 0; x < im->width; x += 8){
	    int i, d = 0;
	    for(i = 0; i < 8 && x + i < im->width; i++){
		d |= (!!im->data[y][(x + i) * 4 + 3]) << i;
		if(im->data[y][(x + i) * 4 + 3] > 0 &&
		   im->data[y][(x + i) * 4 + 3] < 255){
		    transparent = 1;
		}
	    }
	    data[x / 8 + y * w8] = d;
	}
    }

    maskp = XCreateBitmapFromData(xd, w, data, im->width, im->height);
    XShapeCombineMask(xd, w, ShapeBounding, 0, 0, maskp, op);
    XSync(xd, False);
    if(shape)
	*shape = maskp;
    else
	XFreePixmap(xd, maskp);
    free(data);

    return transparent;
}

extern int
merge_shape(window_t *d, Window s, int x, int y)
{
    while(d){
	XShapeCombineShape(xd, d->xw, ShapeBounding, x, y, s,
			   ShapeBounding, ShapeUnion);
	s = d->xw;
	x = d->x;
	y = d->y;
	d = d->parent;
    }

    return 0;
}

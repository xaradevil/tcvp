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

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include "tcvpx.h"
#include "tcvpctl.h"
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


int mapped=1;

Atom XdndDrop, XdndType, XdndSelection, XdndFinished, tcxa;

Display *xd;
int xs;
GC wgc, bgc;
Pixmap root;
int root_width;
int root_height;
int depth;
tcwidget_t *drag;

extern void *
x11_event(void *p)
{
    skin_t *skin = p;

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
	    if(skin->background->transparent) {
		repaint_widgets();
		draw_widgets();
	    }
	    break;

 	case ButtonRelease:
	{
	    if(drag){
		if(drag->common.drag_end){
		    drag->common.drag_end(drag, &xe);
		    drag = NULL;
		}
	    }
	    break;
	}

 	case MotionNotify:
	{
	    if(drag){
		if(drag->common.ondrag){
		    drag->common.ondrag(drag, &xe);
		}
	    }
	    break;
	}

 	case ButtonPress:
	{
	    list_item *current=NULL;
	    tcwidget_t *w;

	    while((w = list_next(click_list, &current))!=NULL) {
		if(xe.xbutton.window == w->common.win){
		    if(w->common.onclick){
			w->common.onclick(w, &xe);
		    }
		}
	    }

	    while((w = list_next(drag_list, &current))!=NULL) {
		if(xe.xbutton.window == w->common.win){
		    if(w->common.enabled) {
			drag = w;
			if(w->common.drag_begin){
			    w->common.drag_begin(w, &xe);
			}
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

	case SelectionNotify: {
	    Atom ret_type;
	    int ret_format;
	    unsigned long ret_item;
	    unsigned long remain_byte;
	    unsigned char *foo;
	    char *buf;

	    XEvent xevent;
	    Window selowner = XGetSelectionOwner(xd, XdndSelection);

	    XGetWindowProperty(xd, xe.xselection.requestor,
			       tcxa, 0, 65536, True, XdndType,
			       &ret_type, &ret_format,
			       &ret_item, &remain_byte,
			       (unsigned char **)&foo);

	    if(foo == NULL)
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

	    for(;;) {
		char *tmp = strchr(buf, '\n');
		if(tmp != NULL) {
		    *tmp=0;
		}

		tcvp_add_file(buf);

		if(tmp == NULL) {
		    break;
		}
		buf = tmp+1;
	    }

	    free(foo);

	    break;
	}

	case ClientMessage:
	    if(xe.xclient.message_type == XdndDrop) {
		XConvertSelection(xd, XdndSelection, XdndType, tcxa,
				  xe.xclient.window, CurrentTime);
	    }
	    break;
	}
    }

    return NULL;
}


extern int
init_graphics()
{
    click_list = list_new(TC_LOCK_SLOPPY);
    drag_list = list_new(TC_LOCK_SLOPPY);
    widget_list = list_new(TC_LOCK_SLOPPY);
    sl_list = list_new(TC_LOCK_SLOPPY);
    skin_list = list_new(TC_LOCK_SLOPPY);

    XInitThreads();
    xd = XOpenDisplay(NULL);

    XSetCloseDownMode (xd, DestroyAll);
    xs = DefaultScreen (xd);

    root_width = XDisplayWidth(xd,xs);
    root_height = XDisplayHeight(xd,xs);
    depth = DefaultDepth(xd, xs);

    root = XCreatePixmap(xd, RootWindow(xd, xs), root_width,
			 root_height, depth);

    return 0;
}


extern int 
create_window(skin_t *skin)
{
    Atom prop;
    MWMHints mwmhints;
    XClassHint *classhints;
    XSizeHints *sizehints;
    XTextProperty windowName;
    char *title = "TCVP";
    Atom xdndversion = 4, xa_atom, xa_cardinal;

    skin->xw = XCreateWindow(xd, RootWindow(xd, xs), 0, 0,
			     skin->width, skin->height, 0,
			     CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);

    XSelectInput(xd, skin->xw, ExposureMask | StructureNotifyMask);

    xa_atom = XInternAtom(xd, "ATOM", False);
    xa_cardinal = XInternAtom(xd, "CARDINAL", False);

    XdndDrop = XInternAtom(xd, "XdndDrop", False);
    XdndType = XInternAtom(xd, "text/plain", False);
    XdndFinished = XInternAtom(xd, "XdndFinished", False);
    XdndSelection = XInternAtom(xd, "XdndSelection", False);
    tcxa = XInternAtom(xd, "TCVPSEL", False);

    memset(&mwmhints, 0, sizeof(MWMHints));
    prop = XInternAtom(xd, "_MOTIF_WM_HINTS", False);
    mwmhints.flags = MWM_HINTS_DECORATIONS;
    mwmhints.decorations = 0;
    XChangeProperty(xd, skin->xw, prop, prop, 32, PropModeReplace,
		    (unsigned char *) &mwmhints,
		    PROP_MWM_HINTS_ELEMENTS);

    if(tcvp_ui_tcvpx_conf_always_on_top != 0) {
	Atom xa_wm_desktop;
	int desktop = -1;

	xa_wm_desktop = XInternAtom(xd, "_NET_WM_DESKTOP", False);

	XChangeProperty(xd, skin->xw, xa_wm_desktop, xa_cardinal, 32,
			PropModeReplace, (unsigned char *) &desktop, 1);
    } else if(tcvp_ui_tcvpx_conf_sticky != 0) {
	Atom xa_wm_state, xa_on_top, xa_win_state;
	int i=1;

	xa_wm_state = XInternAtom(xd, "_NET_WM_STATE", False);
	xa_on_top = XInternAtom(xd, "_NET_WM_STATE_STAYS_ON_TOP", False);

	XChangeProperty(xd, skin->xw, xa_wm_state, xa_atom, 32,
			PropModeReplace, (unsigned char *) &xa_on_top, 1);

	xa_win_state = XInternAtom(xd, "_WIN_STATE", False);
	XChangeProperty(xd, skin->xw, xa_win_state, xa_cardinal, 32,
			PropModeReplace, (unsigned char *) &i, 1);
    }


    prop = XInternAtom(xd, "XdndAware", False);
    XChangeProperty(xd, skin->xw, prop, xa_atom, 32,
		    PropModeReplace, (unsigned char *) &xdndversion, 1);

    skin->bgc = XCreateGC (xd, skin->xw, 0, NULL);
    XSetBackground(xd, skin->bgc, 0x00000000);
    XSetForeground(xd, skin->bgc, 0xFFFFFFFF);

    skin->wgc = XCreateGC (xd, skin->xw, 0, NULL);
    XSetBackground(xd, skin->bgc, 0xFFFFFFFF);
    XSetForeground(xd, skin->bgc, 0x00000000);

    classhints = XAllocClassHint ();
    classhints->res_name = "TCVP";
    classhints->res_class = "TCVP";

    sizehints = XAllocSizeHints ();
    sizehints->flags = PSize | PMinSize | PMaxSize;
    sizehints->min_width = skin->width;
    sizehints->min_height = skin->height;
    sizehints->max_width = skin->width;
    sizehints->max_height = skin->height;

    XStringListToTextProperty (&title, 1, &windowName);
    XSetWMProperties (xd, skin->xw, &windowName, NULL, NULL,
		      0, sizehints, NULL, classhints);

    XFree(sizehints);
    XFree(classhints);

    XSync(xd, False);

    return 0;
}


extern int 
destroy_window(skin_t *skin)
{
    tcwidget_t *w;

    list_delete(skin_list, skin, widget_cmp, NULL);

    while((w = list_pop(skin->widgets))!=NULL) {
	destroy_widget(w);
    }

    list_destroy(skin->widgets, NULL);
    free(skin->path);
    free(skin);

    return 0;
}

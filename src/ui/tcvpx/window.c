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

Display *xd;
int xs;
Window xw;
GC wgc, bgc;
Pixmap root;
int root_width;
int root_height;
int depth;
tcwidget_t *drag;

extern void *
x11_event(void *p)
{
    int run=1;
    skin_t *skin = p;

    while(run){
        XEvent xe;

        XNextEvent(xd, &xe);
/* 	fprintf(stderr, "%d\n", xe.type); */
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

	case DestroyNotify:
	    run = 0;
	    break;
	}
    }

    return NULL;
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

    click_list = list_new(TC_LOCK_SLOPPY);
    drag_list = list_new(TC_LOCK_SLOPPY);
    widget_list = list_new(TC_LOCK_SLOPPY);
    sl_list = list_new(TC_LOCK_SLOPPY);

    XInitThreads();
    xd = XOpenDisplay(NULL);

    XSetCloseDownMode (xd, DestroyAll);
    xs = DefaultScreen (xd);

    root_width = XDisplayWidth(xd,xs);
    root_height = XDisplayHeight(xd,xs);
    depth = DefaultDepth(xd, xs);

    xw = XCreateWindow (xd, RootWindow(xd, xs), 0, 0,
			skin->width, skin->height, 0,
			CopyFromParent, InputOutput,
			CopyFromParent, 0, 0);

    XSelectInput(xd, xw, ExposureMask | StructureNotifyMask);

    memset(&mwmhints, 0, sizeof(MWMHints));
    prop = XInternAtom(xd, "_MOTIF_WM_HINTS", False);
    mwmhints.flags = MWM_HINTS_DECORATIONS;
    mwmhints.decorations = 0;
  
    XChangeProperty(xd, xw, prop, prop, 32, PropModeReplace,
		    (unsigned char *) &mwmhints,
		    PROP_MWM_HINTS_ELEMENTS);

    bgc = XCreateGC (xd, xw, 0, NULL);
    XSetBackground(xd, bgc, 0x00000000);
    XSetForeground(xd, bgc, 0xFFFFFFFF);

    wgc = XCreateGC (xd, xw, 0, NULL);
    XSetBackground(xd, bgc, 0xFFFFFFFF);
    XSetForeground(xd, bgc, 0x00000000);

    root = XCreatePixmap(xd, xw, root_width, root_height, depth);

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
    XSetWMProperties (xd, xw, &windowName, NULL, NULL,
		      0, sizehints, NULL, classhints);

    XFree(sizehints);
    XFree(classhints);

    XSync(xd, False);

    return 0;
}

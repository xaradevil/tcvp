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
#include <string.h>
#include <unistd.h>

extern void *
scroll_labels(void *p)
{
    while(!quit) {
	int c = 0;
	list_item *current=NULL;
	tcwidget_t *w;

	usleep(100000);

	while((w = list_next(sl_list, &current))!=NULL) {
	    if(w->label.window->mapped == 1 && w->label.window->enabled == 1) {
		if((w->label.scrolling & TCLABELMANUAL) == 0) {
		    if(w->label.scrolling & TCLABELSCROLLING) {
			w->label.s_pos++;
			w->label.s_pos %= w->label.s_max;
			if(w->common.repaint) w->common.repaint((xtk_widget_t *)w);
			draw_widget(w);
			c = 1;
		    } else if(w->label.scrolling & TCLABELPINGPONG) {
			w->label.s_pos += w->label.s_dir;
			if(w->label.s_pos > w->label.s_max-1){
			    w->label.s_dir = -1;
			    w->label.s_pos = w->label.s_max-1;
			} else if(w->label.s_pos <= 0){
			    w->label.s_dir = 1;
			    w->label.s_pos = 0;
			}
			if(w->common.repaint) w->common.repaint((xtk_widget_t *)w);
			draw_widget(w);
			c = 1;
		    }
		}
	    }
	    if(c) {
		XSync(xd, False);
	    }
	}
	
    }

    return NULL;
}


static int
alpha_render_text(unsigned char *src, unsigned char *dest, int width,
		  int height, int s_width, int s_height, int xoff,
		  int yoff, int depth, uint32_t color)
{
    int x,y;
    unsigned char red, green, blue;

    blue =   (color & 0xff);
    green = (color & 0xff00)>>8;
    red =  (color & 0xff0000)>>16;

    for(y=0;y<s_height;y++){
	for(x=0;x<s_width;x++){
	    int spos = (x+y*s_width)*4;
	    int dpos = (x+xoff+(y+yoff)*width)*4;
	    int b = src[spos];
	    int a = 256-b;
	    dest[dpos+0] = (dest[dpos+0]*a + red*b)/256;
	    dest[dpos+1] = (dest[dpos+1]*a + green*b)/256;
	    dest[dpos+2] = (dest[dpos+2]*a + blue*b)/256;
	}
    }
    
    return 0;
}


extern int
change_label(tclabel_t *txt, char *text)
{
    if(txt->window->enabled == 1) {
	GC bgc = txt->window->bgc;
	free(txt->text);
	txt->text = strdup((text)?text:"");
	XGlyphInfo xgi;
	XftTextExtents8(xd, txt->xftfont, txt->text, strlen(txt->text), &xgi);
	txt->text_width = xgi.width+2;

/* 	printf("\"%s\"\nwidth:%d height:%d x:%d y:%d xOff:%d yOff:%d\n", */
/* 	       txt->text, xgi.width, xgi.height, xgi.x, xgi.y, */
/* 	       xgi.xOff, xgi.yOff); */

	if(xgi.width+2 > txt->width) {
	    txt->scrolling = txt->scroll;
	} else {
	    txt->scrolling = TCLABELSTANDARD;
	}

	if(txt->scrolling & TCLABELPINGPONG){
	    txt->s_width = xgi.width + txt->s_space + 2;
	} else if(txt->scrolling & TCLABELSCROLLING){
	    txt->s_width = xgi.width + 2;
	} else {
	    txt->s_width = (xgi.width+2 > txt->width)?txt->width:xgi.width + 2;
	}

	if(txt->s_text) XFreePixmap(xd, txt->s_text);
	if(txt->xftdraw) XftDrawDestroy(txt->xftdraw);
	txt->s_text = XCreatePixmap(xd, txt->window->xw, txt->s_width,
				    txt->height, depth);
	txt->xftdraw = XftDrawCreate(xd, txt->s_text,
				     DefaultVisual(xd, xs),
				     DefaultColormap(xd, xs));

	XFillRectangle(xd, txt->s_text, bgc, 0, 0,
		       txt->s_width, txt->height);

	if(txt->scrolling & TCLABELSCROLLING) {
	    txt->s_max = txt->s_width + txt->s_space;
	    txt->s_pos = 0;
	    XftDrawString8(txt->xftdraw, &txt->xftcolor,
			   txt->xftfont, txt->xoff,
			   txt->yoff, txt->text,
			   strlen(txt->text));
	} else if(txt->scrolling & TCLABELPINGPONG){
	    txt->s_max = txt->s_width - txt->width;
	    txt->s_pos = txt->s_space/2;
	    txt->s_dir = 1;
	    XftDrawString8(txt->xftdraw, &txt->xftcolor,
			   txt->xftfont, txt->xoff + txt->s_space/2,
			   txt->yoff, txt->text,
			   strlen(txt->text));
	} else {
	    if(txt->scroll & TCLABELSTANDARD){
		txt->s_pos = 0;
	    } else {
		txt->s_pos = (txt->width - txt->s_width)/2;
	    }
	    XftDrawString8(txt->xftdraw, &txt->xftcolor,
			   txt->xftfont, txt->xoff, txt->yoff,
			   txt->text, strlen(txt->text));
	}

	if(txt->window->mapped==1){
	    txt->repaint((xtk_widget_t *) txt);
	    draw_widget((tcwidget_t *) txt);
	    XSync(xd, False);
	}
    }
    return 0;
}


extern int
repaint_label(xtk_widget_t *xw)
{
    tcwidget_t *txt = (tcwidget_t *)xw;

    GC bgc = txt->label.window->bgc;

    if(txt->label.window->mapped==1){
	if(txt->label.window->enabled == 1) {
#if 0
	    XCopyArea(xd, txt->label.s_text, txt->label.pixmap,
		      txt->label.window->bgc, txt->label.s_pos, 0,
		      txt->label.s_width, txt->label.height, 0, 0);
	    XSync(xd, False);
#else
	    if(txt->label.scrolling & TCLABELSTANDARD){	    
		XImage *img, *text;

		img = XGetImage(xd, txt->label.window->background->pixmap,
				txt->label.x, txt->label.y,
				txt->label.width, txt->label.height,
				AllPlanes, ZPixmap);
		text = XGetImage(xd, txt->label.s_text, 0, 0,
				 txt->label.s_width, txt->label.height,
				 AllPlanes, ZPixmap);
		XSync(xd, False);

		alpha_render_text(text->data, img->data, txt->label.width,
				  txt->label.height, txt->label.s_width,
				  txt->label.height, txt->label.s_pos, 0,
				  depth, txt->label.color);

		XPutImage(xd, txt->label.pixmap, bgc, img, 0, 0, 0, 0,
			  txt->label.width, txt->label.height);
		XSync(xd, False);
		XDestroyImage(img);
		XDestroyImage(text);
	    } else if(txt->label.scrolling & TCLABELSCROLLING) {
		XImage *img, *text;
		Pixmap pmap;

		img = XGetImage(xd, txt->label.window->background->pixmap,
				txt->label.x, txt->label.y,
				txt->label.width, txt->label.height,
				AllPlanes, ZPixmap);

		pmap = XCreatePixmap(xd, txt->label.window->xw, txt->label.width,
				     txt->label.height, depth);
		XFillRectangle(xd, pmap, bgc, 0, 0,
			       txt->label.width, txt->label.height);
		
		if(txt->label.s_pos < txt->label.s_width - txt->label.width) {
		    XCopyArea(xd, txt->label.s_text, pmap, bgc,
			      txt->label.s_pos, 0, txt->label.width,
			      txt->label.height, 0, 0);
		} else {
		    if(txt->label.s_pos < txt->label.s_width) {
			XCopyArea(xd, txt->label.s_text, pmap, bgc,
				  txt->label.s_pos, 0,
				  txt->label.s_width - txt->label.s_pos,
				  txt->label.height, 0, 0);
		    }
		    if(txt->label.s_pos >= txt->label.s_width + txt->label.s_space - txt->label.width) {
			XCopyArea(xd, txt->label.s_text, pmap, bgc, 0, 0,
				  txt->label.width - (txt->label.s_width - txt->label.s_pos),
				  txt->label.height,
				  (txt->label.s_width - txt->label.s_pos + txt->label.s_space), 0);
		    }
		}

		text = XGetImage(xd, pmap, 0, 0,
				 txt->label.width, txt->label.height,
				 AllPlanes, ZPixmap);
		XSync(xd, False);

		alpha_render_text(text->data, img->data, txt->label.width,
				  txt->label.height, txt->label.width,
				  txt->label.height, 0, 0, depth,
				  txt->label.color);

		XPutImage(xd, txt->label.pixmap, bgc, img, 0, 0, 0, 0,
			  txt->label.width, txt->label.height);
		XSync(xd, False);
		XDestroyImage(img);
		XDestroyImage(text);
		XFreePixmap(xd, pmap);
	    } else if(txt->label.scrolling & TCLABELPINGPONG) {
		XImage *img, *text;

		img = XGetImage(xd, txt->label.window->background->pixmap,
				txt->label.x, txt->label.y,
				txt->label.width, txt->label.height,
				AllPlanes, ZPixmap);
		text = XGetImage(xd, txt->label.s_text, txt->label.s_pos, 0,
				 txt->label.width, txt->label.height,
				 AllPlanes, ZPixmap);
		XSync(xd, False);

		alpha_render_text(text->data, img->data, txt->label.width,
				  txt->label.height, txt->label.width,
				  txt->label.height, 0, 0, depth,
				  txt->label.color);

		XPutImage(xd, txt->label.pixmap, bgc, img, 0, 0, 0, 0,
			  txt->label.width, txt->label.height);
		XSync(xd, False);
		XDestroyImage(img);
		XDestroyImage(text);
	    }
#endif
	}
    }
    return 0;
}


static int
label_ondrag(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if(w->label.scrolling & TCLABELMANUAL) {
	w->label.s_pos =  w->label.sdrag - (((XEvent *)xe)->xmotion.x -
					    w->label.xdrag);

	if(w->label.s_pos>=w->label.s_max) {
	    w->label.s_pos = w->label.s_max-1;
	}
	if(w->label.s_pos<0) {
	    w->label.s_pos = 0;
	}

	w->label.repaint((xtk_widget_t *)w);
	draw_widget(w);
	XSync(xd, False);
    }

    return 0;
}


static int
label_drag_begin(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if((w->label.scrolling & TCLABELSTANDARD) == 0) {
	w->label.scrolling |= TCLABELMANUAL;
	w->label.xdrag = ((XEvent *)xe)->xbutton.x;
	w->label.sdrag = w->label.s_pos;
    }

    return 0;
}


static int
label_drag_end(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if((w->label.scrolling & TCLABELSTANDARD) == 0) {
	w->label.scrolling = w->label.scroll;
    }

    return 0;
}


static int
destroy_label(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if((w->label.scroll & TCLABELSTANDARD)==0) {
	list_delete(sl_list, w, widget_cmp, NULL);
	list_delete(drag_list, w, widget_cmp, NULL);
    }

    if(w->label.xftdraw) XftDrawDestroy(w->label.xftdraw);
    if(w->label.s_text) XFreePixmap(xd, w->label.s_text);

    return 0;
}


extern tclabel_t*
create_label(window_t *window, int x, int y, int width, int height,
	     int xoff, int yoff, char *text, char *font,
	     char *color, short alpha, int scroll, action_cb_t action,
	     void *data)
{
    long emask;
    XColor xc;
    XParseColor(xd, DefaultColormap(xd, xs), color, &xc);

    tclabel_t *txt = calloc(sizeof(tclabel_t), 1);
    XRenderColor xrc = {
	.red = 0xffff,
	.green = 0xffff,
	.blue = 0xffff,
	.alpha = (alpha<<8) + alpha
    };

    int r = (xc.red & 0xff00)>>8;
    int g = (xc.green & 0xff00)>>8;
    int b = (xc.blue & 0xff00)>>8;
    int c = (alpha<<24) + (b<<16) + (g<<8) + r;

    txt->type = TCLABEL;
    txt->x = x;
    txt->y = y;
    txt->width = width;
    txt->height = height;
    txt->repaint = repaint_label;
    txt->destroy = destroy_label;
    txt->window = window;
    txt->font = strdup(font);
    txt->colorname = color;
    txt->color = c;
    txt->alpha = alpha;
    txt->xoff = xoff;
    txt->yoff = yoff;
    txt->scroll = scroll;
    txt->s_pos = 0;
    txt->s_max = 0;
    txt->s_space = 20;
    txt->s_dir = 1;
    txt->enabled = 1;
    txt->data = data;


    XftColorAllocValue(xd, DefaultVisual(xd, xs), DefaultColormap(xd, xs),
		       &xrc, &txt->xftcolor);

    txt->xftfont = XftFontOpenName(xd, xs, font);

    if(txt->xftfont==NULL){
	fprintf(stderr, "font \"%s\" not found\n",
		txt->font);
	return NULL;
    }

    txt->pixmap = XCreatePixmap(xd, window->xw, txt->width, txt->height, depth);

    txt->win = XCreateWindow(xd, window->xw, txt->x, txt->y,
			     txt->width, txt->height,
			     0, CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);

    change_label(txt, text);

    emask = ExposureMask;
    list_push(widget_list, txt);
    if(action){
	txt->onclick = widget_onclick;
	txt->action = action;
	list_push(click_list, txt);
	emask |= ButtonPressMask;
    }
    if((txt->scroll & TCLABELSTANDARD)==0) {
	list_push(sl_list, txt);
	list_push(drag_list, txt);
	txt->ondrag = label_ondrag;
	txt->drag_begin = label_drag_begin;
	txt->drag_end = label_drag_end;
	emask |= ButtonPressMask | PointerMotionMask | ButtonReleaseMask;
    }

    XSelectInput(xd, txt->win, emask);

    list_push(window->widgets, txt);

    return txt;
}

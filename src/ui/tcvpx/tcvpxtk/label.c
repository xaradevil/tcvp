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
#include <string.h>
#include <unistd.h>

#define min(a, b) ((a)<(b)?(a):(b))
#define max(a, b) ((a)>(b)?(a):(b))

extern void *
scroll_labels(void *p)
{
    while(!quit) {
	int c = 0;
	tclist_item_t *current=NULL;
	tcwidget_t *w;

	usleep(100000);

	while((w = tclist_next(sl_list, &current))!=NULL) {
	    if(w->label.window->mapped == 1 && w->label.window->enabled == 1) {
		if((w->label.scrolling & TCLABELMANUAL) == 0) {
		    if(w->label.scrolling & TCLABELSCROLLING) {
			w->label.s_pos--;
			if(w->label.s_pos < -w->label.s_max)
			    w->label.s_pos = 0;
			if(w->common.repaint) w->common.repaint((xtk_widget_t *)w);
			draw_widget(w);
			c = 1;
		    } else if(w->label.scrolling & TCLABELPINGPONG) {
			w->label.s_pos += w->label.s_dir;
			if(w->label.s_pos < -w->label.s_max + 1){
			    w->label.s_dir = 1;
			    w->label.s_pos = -w->label.s_max + 1;
			} else if(w->label.s_pos > 0){
			    w->label.s_dir = -1;
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
    int x, y, w;
    unsigned char red, green, blue;

    blue =   (color & 0xff);
    green = (color & 0xff00)>>8;
    red =  (color & 0xff0000)>>16;

    w = min(s_width, width - xoff);

    for(y=0;y<s_height;y++){
	for(x=0;x<w;x++){
	    if(x < width && y < height) {
		int spos = (x+y*s_width)*4;
		int dpos = (x+xoff+(y+yoff)*width)*4;
		int b = src[spos];
		int a = 256-b;
		dest[dpos+0] = (dest[dpos+0]*a + red*b)/256;
		dest[dpos+1] = (dest[dpos+1]*a + green*b)/256;
		dest[dpos+2] = (dest[dpos+2]*a + blue*b)/256;
	    } else {
		fprintf(stderr, "ERROR: Attempted to draw outside widget\n");
	    }
	}
    }
    
    return 0;
}


extern int
change_label(xtk_widget_t *xtxt, char *text)
{
    tclabel_t *txt = (tclabel_t *) xtxt;
    if(txt->window->enabled == 1) {
	GC bgc = txt->window->bgc;
	int xoff = 0;

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
	    xoff = txt->s_space / 2;
	} else {
	    txt->s_width = xgi.width + 2;
	    if(txt->scrolling & TCLABELSCROLLING){
		txt->s_width += txt->width + txt->s_space;
	    }
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

	XftDrawString8(txt->xftdraw, &txt->xftcolor,
		       txt->xftfont, txt->xoff + xoff + xgi.x,
		       txt->yoff + xgi.y, txt->text,
		       strlen(txt->text));

	XSync(xd, False);

	if(txt->scrolling & TCLABELSCROLLING) {
	    txt->s_max = xgi.width + 2 + txt->s_space;
	    txt->s_pos = 0;
	    if(txt->s_width > txt->width){
		XCopyArea(xd, txt->s_text, txt->s_text, bgc,
			  0, 0, txt->width, txt->height,
			  xgi.width + 2 + txt->s_space, 0);
	    }
	} else if(txt->scrolling & TCLABELPINGPONG){
	    txt->s_max = txt->s_width - txt->width;
	    txt->s_pos = - txt->s_space / 2;
	    txt->s_dir = -1;
	} else {
	    if(txt->align == TCLABELLEFT) {
		txt->s_pos = 0;
	    } else if(txt->align == TCLABELRIGHT) {
		txt->s_pos = txt->width - txt->s_width;
	    } else {
		txt->s_pos = (txt->width - txt->s_width) / 2;
	    }
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
	    XImage *img, *text;
	    int rsx, rdx;

	    img = XGetImage(xd, txt->label.window->background->pixmap,
			    txt->label.x, txt->label.y,
			    txt->label.width, txt->label.height,
			    AllPlanes, ZPixmap);
	    text = XGetImage(xd, txt->label.s_text, 0, 0,
			     txt->label.s_width, txt->label.height,
			     AllPlanes, ZPixmap);
	    XSync(xd, False);

	    if(txt->label.background) {
		alpha_render(*txt->label.background->data, img->data,
			     img->width, img->height, depth);
	    }

	    if(txt->label.s_pos < 0){
		rsx = -txt->label.s_pos;
		rdx = 0;
	    } else {
		rsx = 0;
		rdx = txt->label.s_pos;
	    }

	    alpha_render_text(text->data + rsx * 4, img->data,
			      txt->label.width, txt->label.height,
			      txt->label.s_width, txt->label.height,
			      rdx, 0, depth, txt->label.color);

	    XPutImage(xd, txt->label.pixmap, bgc, img, 0, 0, 0, 0,
		      txt->label.width, txt->label.height);
	    XSync(xd, False);
	    XDestroyImage(img);
	    XDestroyImage(text);
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
	w->label.s_pos = w->label.sdrag + (((XEvent *)xe)->xmotion.x -
					   w->label.xdrag);

	if(w->label.s_pos < -w->label.s_max) {
	    w->label.s_pos = -w->label.s_max;
	}
	if(w->label.s_pos > 0) {
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
	tclist_delete(sl_list, w, widget_cmp, NULL);
    }

    if(w->label.xftdraw) XftDrawDestroy(w->label.xftdraw);
    if(w->label.s_text) XFreePixmap(xd, w->label.s_text);
    XftFontClose(xd, w->label.xftfont);

    if(w->label.background) {
	image_free(w->label.background);
	free(w->label.background);
    }

    free(w->label.text);
    free(w->label.font);
    free(w->label.colorname);
    return 0;
}


extern xtk_widget_t*
create_label(window_t *window, int x, int y, int width, int height,
	     int xoff, int yoff, image_info_t *bg, char *text, char *font,
	     char *color, short alpha, int scroll, int align,
	     action_cb_t action, void *data)
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
    txt->colorname = strdup(color);
    txt->color = c;
    txt->alpha = alpha;
    txt->xoff = xoff;
    txt->yoff = yoff;
    txt->background = bg;
    txt->align = align;
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
	fprintf(stderr, "font \"%s\" not found\n", txt->font);
	return NULL;
    }

    txt->pixmap = XCreatePixmap(xd, window->xw, txt->width, txt->height, depth);

    txt->win = XCreateWindow(xd, window->xw, txt->x, txt->y,
			     txt->width, txt->height,
			     0, CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);
    merge_shape(window, txt->win, x, y);

    change_label((xtk_widget_t *) txt, text);

    emask = ExposureMask;
    tclist_push(widget_list, txt);
    if(action){
	txt->onclick = widget_onclick;
	txt->action = action;
	emask |= ButtonPressMask | ButtonReleaseMask | EnterWindowMask |
	    LeaveWindowMask;
    }
    if((txt->scroll & TCLABELSTANDARD)==0) {
	tclist_push(sl_list, txt);
	txt->ondrag = label_ondrag;
	txt->drag_begin = label_drag_begin;
	txt->drag_end = label_drag_end;
	emask |= ButtonPressMask | PointerMotionMask | ButtonReleaseMask;
    }
    if(action || (txt->scroll & TCLABELSTANDARD)==0){
	tclist_push(click_list, txt);
    }

    XSelectInput(xd, txt->win, emask);

    tclist_push(window->widgets, txt);

    return (xtk_widget_t *) txt;
}

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

#include "tcvpx.h"
#include <string.h>

static int
alpha_render_text(unsigned char *src, unsigned char *dest, int width,
		  int height, int depth, uint32_t color)
{
    int x,y;
    unsigned char red, green, blue;

    red =   (color & 0xff);
    green = (color & 0xff00)>>8;
    blue =  (color & 0xff0000)>>16;

    for(y=0;y<height;y++){
	for(x=0;x<width;x++){
	    int pos = (x+y*width)*4;
	    int b = src[pos];
	    int a = 256-b;
	    dest[pos+0] = (dest[pos+0]*a + red*b)/256;
	    dest[pos+1] = (dest[pos+1]*a + green*b)/256;
	    dest[pos+2] = (dest[pos+2]*a + blue*b)/256;
	}
    }
    
    return 0;
}


extern int
change_label(tclabel_t *txt, char *text)
{
    free(txt->text);
    txt->text = strdup((text)?text:"");
    if(mapped==1){
	if(txt->scroll != TCLABELSTANDARD) {
	    XGlyphInfo xgi;
	    XftTextExtents8(xd, txt->xftfont, text, strlen(text), &xgi);
	    txt->text_width = xgi.width+2;
/* 	    printf("\"%s\"\nwidth:%d height:%d x:%d y:%d xOff:%d yOff:%d\n", */
/* 		   text, xgi.width, xgi.height, xgi.x, xgi.y, */
/* 		   xgi.xOff, xgi.yOff); */

	    if(xgi.width+2 > txt->width) {
		txt->scrolling = txt->scroll;
		if(txt->scroll == TCLABELSCROLLING) {
		    txt->s_width = xgi.width + 2;
		} else if(txt->scroll == TCLABELPINGPONG){
		    txt->s_width = xgi.width + txt->s_space + 2;
		}
		    
		if(txt->s_text) XFreePixmap(xd, txt->s_text);
		if(txt->xftdraw) XftDrawDestroy(txt->xftdraw);
		txt->s_text = XCreatePixmap(xd, xw, txt->s_width,
					    txt->height, depth);
		txt->xftdraw = XftDrawCreate(xd, txt->s_text,
					     DefaultVisual(xd, xs),
					     DefaultColormap(xd, xs));

		XFillRectangle(xd, txt->s_text, bgc, 0, 0,
			       txt->s_width, txt->height);
		if(txt->scroll == TCLABELSCROLLING) {
		    txt->s_max = txt->s_width + txt->s_space;
		    txt->s_pos = 0;
		    XftDrawString8(txt->xftdraw, &txt->scroll_xftcolor,
				   txt->xftfont, txt->xoff,
				   txt->yoff, txt->text,
				   strlen(txt->text));
		} else if(txt->scroll == TCLABELPINGPONG){
		    txt->s_max = txt->s_width - txt->width;
		    txt->s_pos = txt->s_space/2;
		    txt->s_dir = 1;
		    XftDrawString8(txt->xftdraw, &txt->scroll_xftcolor,
				   txt->xftfont, txt->xoff + txt->s_space/2,
				   txt->yoff, txt->text,
				   strlen(txt->text));
		}
	    } else {
		txt->xftdraw = XftDrawCreate(xd, txt->pixmap,
					     DefaultVisual(xd, xs),
					     DefaultColormap(xd, xs));
		txt->scrolling = TCLABELSTANDARD;
	    }		
	} else {
	    txt->scrolling = TCLABELSTANDARD;
	}
	txt->repaint((tcwidget_t *) txt);
	draw_widget((tcwidget_t *) txt);
	XSync(xd, False);
    }
    return 0;
}


extern int
repaint_label(tcwidget_t *txt)
{
    if(mapped==1){
	if(txt->label.scrolling == TCLABELSTANDARD){
	    XCopyArea(xd, txt->label.skin->background->pixmap,
		      txt->label.pixmap, bgc,
		      txt->label.x, txt->label.y,
		      txt->label.width, txt->label.height, 0, 0);
	    if(txt->label.scroll == TCLABELSTANDARD){
		XftDrawString8(txt->label.xftdraw, &txt->label.xftcolor,
			       txt->label.xftfont, txt->label.xoff,
			       txt->label.yoff, txt->label.text,
			       strlen(txt->label.text));
	    } else {
		XftDrawString8(txt->label.xftdraw, &txt->label.xftcolor,
			       txt->label.xftfont, txt->label.xoff + 
			       (txt->label.width - txt->label.text_width)/2,
			       txt->label.yoff, txt->label.text,
			       strlen(txt->label.text));
	    }
	} else if(txt->label.scrolling == TCLABELSCROLLING) {
	    XImage *img, *text;
	    Pixmap pmap;

	    img = XGetImage(xd, txt->label.skin->background->pixmap,
			    txt->label.x, txt->label.y,
			    txt->label.width, txt->label.height,
			    AllPlanes, ZPixmap);

	    pmap = XCreatePixmap(xd, xw, txt->label.width, txt->label.height,
				 depth);
	    XFillRectangle(xd, pmap, bgc, 0, 0,
			   txt->label.width, txt->label.height);
	    
	    if(txt->label.s_pos < txt->label.s_width - txt->label.width) {
		XCopyArea(xd, txt->label.s_text, pmap, bgc, txt->label.s_pos,
			  0, txt->label.width, txt->label.height, 0, 0);
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
			      txt->label.height, depth, txt->label.color);

	    XPutImage(xd, txt->label.pixmap, bgc, img, 0, 0, 0, 0,
		      txt->label.width, txt->label.height);
	    XSync(xd, False);
	    XDestroyImage(img);
	    XDestroyImage(text);
	    XFreePixmap(xd, pmap);
	} else if(txt->label.scrolling == TCLABELPINGPONG) {
	    XImage *img, *text;

	    img = XGetImage(xd, txt->label.skin->background->pixmap,
			    txt->label.x, txt->label.y,
			    txt->label.width, txt->label.height,
			    AllPlanes, ZPixmap);
	    text = XGetImage(xd, txt->label.s_text, txt->label.s_pos, 0,
			    txt->label.width, txt->label.height,
			    AllPlanes, ZPixmap);
	    XSync(xd, False);

	    alpha_render_text(text->data, img->data, txt->label.width,
			      txt->label.height, depth, txt->label.color);

	    XPutImage(xd, txt->label.pixmap, bgc, img, 0, 0, 0, 0,
		      txt->label.width, txt->label.height);
	    XSync(xd, False);
	    XDestroyImage(img);
	    XDestroyImage(text);
	}
    }
    return 0;
}


extern tclabel_t*
create_label(skin_t *skin, int x, int y, int width, int height,
	     int xoff, int yoff, char *text, char *font,
	     double size, uint32_t color, int scroll,
	     onclick_cb_t onclick)
{
    tclabel_t *txt = calloc(sizeof(tclabel_t), 1);

    txt->type = TCLABEL;
    txt->x = x;
    txt->y = y;
    txt->width = width;
    txt->height = height;
    txt->onclick = onclick;
    txt->repaint = repaint_label;
    txt->skin = skin;
    txt->font = strdup(font);
    txt->color = color;
    txt->size = size;
    txt->xoff = xoff;
    txt->yoff = yoff;
    txt->scroll = scroll;
    txt->s_pos = 0;
    txt->s_max = 0;
    txt->s_space = 20;
    txt->s_dir = 1;

    int red = (txt->color & 0xff);
    int green = (txt->color & 0xff00)>>8;
    int blue = (txt->color & 0xff0000)>>16;
    int alpha = (txt->color & 0xff000000)>>24;

    XRenderColor xrc = {
	.red = red + (red<<8),
	.green = green + (green<<8),
	.blue = blue + (blue<<8),
	.alpha = alpha + (alpha<<8)
    };
    XRenderColor sxrc = {
	.red = 0xffff,
	.green = 0xffff,
	.blue = 0xffff,
	.alpha = alpha + (alpha<<8)
    };

    XftColorAllocValue(xd, DefaultVisual(xd, xs), DefaultColormap(xd, xs),
		       &xrc, &txt->xftcolor);
    XftColorAllocValue(xd, DefaultVisual(xd, xs), DefaultColormap(xd, xs),
		       &sxrc, &txt->scroll_xftcolor);

    txt->xftfont =
	XftFontOpen(xd, xs, XFT_FAMILY, XftTypeString, txt->font,
		    XFT_PIXEL_SIZE, XftTypeDouble, txt->size, NULL);
    if(txt->xftfont==NULL){
	fprintf(stderr, "font \"%s\" with size %f not found\n",
		txt->font, txt->size);
	return NULL;
    }

    txt->pixmap = XCreatePixmap(xd, xw, txt->width, txt->height, depth);

    if(txt->scrolling == TCLABELSTANDARD) {	
	txt->xftdraw = XftDrawCreate(xd, txt->pixmap, DefaultVisual(xd, xs),
				     DefaultColormap(xd, xs));
    }

    txt->win = XCreateWindow(xd, xw, txt->x, txt->y,
			     txt->width, txt->height,
			     0, CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);

    change_label(txt, text);

    if(txt->scroll != TCLABELSTANDARD) {
	list_push(sl_list, txt);
    }
    list_push(widget_list, txt);
    if(onclick){
	list_push(bt_list, txt);
	XSelectInput(xd, txt->win, ButtonPressMask);
    }

    return txt;
}

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
#include <unistd.h>

list *widget_list, *bt_list, *sl_list;

extern void *
scroll_labels(void *p)
{
    while(!quit) {
	list_item *current=NULL;
	tcwidget_t *w;

	usleep(100000);

	if(mapped==1){
	    while((w = list_next(sl_list, &current))!=NULL) {
		if(w->label.scrolling == TCLABELSCROLLING) {
		    w->label.s_pos++;
		    w->label.s_pos %= w->label.s_max;
		    if(w->common.repaint) w->common.repaint(w);
		    draw_widget(w);
		} else if(w->label.scrolling == TCLABELPINGPONG) {
		    w->label.s_pos += w->label.s_dir;
		    if(w->label.s_pos % w->label.s_max == 0){
			w->label.s_dir *= -1;
		    }
		    if(w->common.repaint) w->common.repaint(w);
		    draw_widget(w);
		}
	    }
	}
	
    }

    return NULL;
}


extern image_info_t*
load_image(char *skinpath, char *file)
{
    FILE *f;
    char fn[1024];
    image_info_t *img;

    snprintf(fn, 1023, "%s/%s", skinpath, file);
    f = fopen(fn,"r");

    img = malloc(sizeof(image_info_t));

    img->flags = IMAGE_COLOR_TYPE | IMAGE_SWAP_ORDER;
    img->color_type = IMAGE_COLOR_TYPE_RGB_ALPHA;
    img->iodata = f;
    img->iofn = (vfs_fread_t)fread;
    image_png_read(img);
    fclose(f);

    return img;
}


extern int
alpha_render(unsigned char *src, unsigned char *dest, int width,
	     int height, int depth)
{
    int x,y;

    for(y=0;y<height;y++){
	for(x=0;x<width;x++){
	    int pos = (x+y*width)*4;
	    int b = src[pos+3];
	    int a = 256-b;
	    dest[pos+0] = (dest[pos+0]*a + src[pos+0]*b)/256;
	    dest[pos+1] = (dest[pos+1]*a + src[pos+1]*b)/256;
	    dest[pos+2] = (dest[pos+2]*a + src[pos+2]*b)/256;
	    dest[pos+3] = (dest[pos+3]*a + src[pos+3]*b)/256;
	}
    }
    
    return 0;
}


extern int
draw_widget(tcwidget_t *w)
{
    if(mapped==1){
	XCopyArea(xd, w->common.pixmap, w->common.win, bgc, 0, 0,
		  w->common.width, w->common.height, 0, 0);
    }
    return 0;
}


extern int
draw_widgets()
{
    list_item *current=NULL;
    tcwidget_t *w;

    if(mapped==1){
	while((w = list_next(widget_list, &current))!=NULL) {
	    draw_widget(w);
	}
    }

    return 0;
}


extern int
repaint_widgets()
{
    list_item *current=NULL;
    tcwidget_t *w;

    if(mapped==1){
	while((w = list_next(widget_list, &current))!=NULL) {
	    if(w->common.repaint) w->common.repaint(w);
	}
    }

    return 0;
}

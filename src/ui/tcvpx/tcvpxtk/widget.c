/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include "widgets.h"
#include <unistd.h>

tclist_t *widget_list, *click_list, *sl_list, *window_list;


extern int
alpha_render_part(unsigned char *src, unsigned char *dest,
		  int src_x, int src_y, int dest_x, int dest_y,
		  int src_width, int src_height, 
		  int dest_width, int dest_height, int depth)
{
    int x,y;

    for(y=src_y;y<src_height;y++){
	for(x=src_x;x<src_width;x++){
	    if(x < dest_width && x >= 0 && y < dest_height && y >= 0) {
		int spos = ((x+src_x)+(y+src_y)*src_width)*4;
		int dpos = ((x+dest_x)+(y+dest_y)*dest_width)*4;
		int b = src[spos+3];
		int a = 256-b;
		dest[dpos+0] = (dest[dpos+0]*a + src[spos+0]*b)/256;
		dest[dpos+1] = (dest[dpos+1]*a + src[spos+1]*b)/256;
		dest[dpos+2] = (dest[dpos+2]*a + src[spos+2]*b)/256;
		dest[dpos+3] = (dest[dpos+3]*a + src[spos+3]*b)/256;
	    } else {
		fprintf(stderr, "ERROR: Attempted to draw outside widget\n");
	    }
	}
    }
    
    return 0;
}


extern int
alpha_render(unsigned char *src, unsigned char *dest,
	     int width, int height, int depth)
{
    return alpha_render_part(src, dest, 0, 0, 0, 0, width, height,
			     width, height, depth);
}


extern int 
draw_window(window_t *win)
{
    if(win->mapped==1){
	tclist_item_t *current=NULL;
	tcwidget_t *w;
	while((w = tclist_next(win->widgets, &current))!=NULL) {
	    draw_widget(w);
	}
    }
    return 0;
}

extern int
draw_widget(tcwidget_t *w)
{
    if(w->common.window->mapped != 0 && w->common.visible != 0){
	XCopyArea(xd, w->common.pixmap, w->common.win,
		  w->common.window->bgc, 0, 0, w->common.width,
		  w->common.height, 0, 0);

	if(w->type == TCBOX && w->box.enabled != 0) {
	    draw_window(w->box.subwindow);
	}
    }
    return 0;
}

extern int
draw_widgets()
{
    tclist_item_t *currentwin=NULL;
    window_t *win;

    while((win = tclist_next(window_list, &currentwin))!=NULL) {
	draw_window(win);
    }

    XSync(xd, False);

    return 0;
}


extern int 
repaint_window(window_t *win)
{
    if(win->mapped==1){
	tcwidget_t *w;
	tclist_item_t *current=NULL;
	while((w = tclist_next(win->widgets, &current))!=NULL) {
	    if(w->common.repaint && w->common.visible != 0) {
		w->common.repaint((xtk_widget_t *)w);
	    }
	    if(w->type == TCBOX && w->box.enabled != 0) {
		repaint_window(w->box.subwindow);
	    }
	}
    }

    return 0;
}

extern int
repaint_widgets()
{
    tclist_item_t *currentwin=NULL;
    window_t *win;

    while((win = tclist_next(window_list, &currentwin))!=NULL) {
	repaint_window(win);
    }

    return 0;
}


extern int
widget_onclick(xtk_widget_t *p, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)p;
    if(w->common.enabled && w->common.action){
	return w->common.action(p, NULL);
    }
    return 0;
}


extern int
widget_cmp(const void *p1, const void *p2)
{
    return p1-p2;
}


extern int
destroy_widget(tcwidget_t *w)
{
    tclist_delete(widget_list, w, widget_cmp, NULL);

    if(w->type == TCLABEL) {
	tclist_delete(sl_list, w, widget_cmp, NULL);
    }

    if(w->common.action || w->common.ondrag){
	tclist_delete(click_list, w, widget_cmp, NULL);
    }

    if(w->common.win) {
	XSelectInput(xd, w->common.win, 0);
    }

    if(w->common.ondestroy) {
	w->common.ondestroy((xtk_widget_t *) w);
    }

    if(w->type != TCBACKGROUND) {
	XDestroyWindow(xd, w->common.win);
    }
    XFreePixmap(xd, w->common.pixmap);

    if(w->common.destroy) {
	w->common.destroy((xtk_widget_t *) w);
    }

    free(w);

    return 0;
}


extern int
show_widget(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *) xw;
    XMapWindow(xd, w->common.win);
    if(w->type == TCBOX) {
	show_window(w->box.subwindow);
    }
    w->common.visible = 1;

    return 0;
}

extern int
hide_widget(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *) xw;
    XUnmapWindow(xd, w->common.win);
    w->common.visible = 0;

    if(w->type == TCBOX) {
	hide_window(w->box.subwindow);
    }

    return 0;
}

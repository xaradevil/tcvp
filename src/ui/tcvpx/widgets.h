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

#ifndef _TCWIDGETS_H
#define _TCWIDGETS_H

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include "skin.h"
#include <tcvpx_tc2.h>

#define TCIMAGEBUTTON 0
#define TCLABEL       1
#define TCBACKGROUND  2
#define TCSLIDER      3

#define TCLABELSTANDARD   0
#define TCLABELSCROLLING  1
#define TCLABELPINGPONG   2

typedef union _tcwidget_t tcwidget_t;
typedef int(*onclick_cb_t)(tcwidget_t *, XEvent *);
typedef int(*repaint_cb_t)(tcwidget_t *);
typedef struct _skin_t skin_t;

typedef struct {
    int type;
    int width, height;
    Pixmap pixmap;
    Window win;    
    onclick_cb_t onclick;
    repaint_cb_t repaint;
    void *data;
    skin_t *skin;
    int x,y;

    image_info_t *img;
    int transparent;
} tcbackground_t;

typedef struct {
    int type;
    int width, height;
    Pixmap pixmap;
    Window win;    
    onclick_cb_t onclick;
    repaint_cb_t repaint;
    void *data;
    skin_t *skin;
    int x,y;

    image_info_t *background;
    image_info_t *slider;
    int pos;    
} tcslider_t;

typedef struct {
    int type;
    int width, height;
    Pixmap pixmap;
    Window win;
    onclick_cb_t onclick;
    repaint_cb_t repaint;
    void *data;
    skin_t *skin;
    int x,y;

    image_info_t *img;
} tcimage_button_t;

typedef struct {
    int type;
    int width, height;
    Pixmap pixmap;
    Window win;
    onclick_cb_t onclick;
    repaint_cb_t repaint;
    void *data;
    skin_t *skin;
    int x,y;

    char *colorname;
    uint32_t color;
    short alpha;
    char *font;
    XftFont *xftfont;
    XftColor xftcolor;
    XftDraw *xftdraw;
    int xoff, yoff;
    int text_width;
    char *text;
    int scroll;
    int scrolling;
    int s_width;
    int s_pos;
    int s_max;
    int s_space;
    int s_dir;
    Pixmap s_text;
} tclabel_t;

typedef struct {
    int type;
    int width, height;
    Pixmap pixmap;
    Window win;    
    onclick_cb_t onclick;
    repaint_cb_t repaint;
    void *data;
    skin_t *skin;
    int x,y;
} tcwidget_common_t;

union _tcwidget_t {
    int type;
    tcwidget_common_t common;
    tcimage_button_t button;
    tclabel_t label;
    tcbackground_t background;
    tcslider_t slider;
};

extern list *widget_list, *bt_list, *sl_list;

int draw_widget(tcwidget_t *w);
int draw_widgets();
int repaint_widgets();
int alpha_render(unsigned char *src, unsigned char *dest, int width,
		 int height, int depth);
image_info_t* load_image(char *skinpath, char *file);

int update_root(skin_t *skin);
tcbackground_t* create_background(skin_t *skin, char *imagefile);

tcimage_button_t* create_button(skin_t *skin, int x, int y,
 			       char *imagefile, onclick_cb_t onclick);

int change_label(tclabel_t *txt, char *text);
tclabel_t* create_label(skin_t *skin, int x, int y, int width, int height,
			int xoff, int yoff, char *text, char *font,
			char *color, short alpha, int scroll,
			onclick_cb_t onclick);


#endif /* _TCWIDGETS_H */

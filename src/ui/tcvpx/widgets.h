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
#define TCSEEKBAR     3
#define TCSTATE       4

#define TCLABELSTANDARD   (1<<0)
#define TCLABELSCROLLING  (1<<1)
#define TCLABELPINGPONG   (1<<2)
#define TCLABELMANUAL     (1<<3)

typedef union _tcwidget_t tcwidget_t;
typedef int(*on_xevent_cb_t)(tcwidget_t *, XEvent *);
typedef int(*action_cb_t)(tcwidget_t *, void *);
typedef int(*widget_cb_t)(tcwidget_t *);
typedef struct _skin_t skin_t;

#define WIDGET_COMMON				\
    int type;					\
    int width, height;				\
    Pixmap pixmap;				\
    Window win;					\
    on_xevent_cb_t onclick;			\
    on_xevent_cb_t drag_begin;			\
    on_xevent_cb_t ondrag;			\
    on_xevent_cb_t drag_end;			\
    on_xevent_cb_t enter;			\
    on_xevent_cb_t exit;			\
    on_xevent_cb_t press;			\
    on_xevent_cb_t release;			\
    action_cb_t action;				\
    widget_cb_t repaint;			\
    widget_cb_t destroy;			\
    void *data;					\
    skin_t *skin;				\
    int x,y;					\
    int enabled

typedef struct {
    WIDGET_COMMON;
    image_info_t *img;
    int transparent;
    Atom xa_rootpmap;
} tcbackground_t;

typedef struct {
    WIDGET_COMMON;
    image_info_t *background;
    image_info_t *indicator;
    int start_x, start_y;
    int end_x, end_y;
    double position;    
} tcseek_bar_t;

typedef struct {
    WIDGET_COMMON;
    image_info_t *img;
    image_info_t *bgimg;
    image_info_t *over_img;
    image_info_t *down_img;
} tcimage_button_t;

typedef struct {
    WIDGET_COMMON;
    int active_state;
    int num_states;
    char **states;
    image_info_t **images;
} tcstate_t;

typedef struct {
    WIDGET_COMMON;
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
    int xdrag;
    int sdrag;
} tclabel_t;

typedef struct {
    WIDGET_COMMON;
} tcwidget_common_t;

union _tcwidget_t {
    int type;
    tcwidget_common_t common;
    tcimage_button_t button;
    tclabel_t label;
    tcbackground_t background;
    tcseek_bar_t seek_bar;
    tcstate_t state;
};

extern list *widget_list, *click_list, *sl_list, *drag_list, *skin_list;

int widget_onclick(tcwidget_t *w, XEvent *xe);
int draw_widget(tcwidget_t *w);
int draw_widgets();
int repaint_widgets();
int destroy_widget(tcwidget_t *w);

int alpha_render(unsigned char *src, unsigned char *dest, int width,
		 int height, int depth);
int alpha_render_part(unsigned char *src, unsigned char *dest,
		      int src_x, int src_y, int dest_x, int dest_y,
		      int src_width, int src_height, 
		      int dest_width, int dest_height, int depth);

image_info_t* load_image(char *skinpath, char *file);

int update_root(skin_t *skin);
tcbackground_t* create_background(skin_t *skin, char *imagefile);

tcimage_button_t* create_button(skin_t *skin, int x, int y,
				char *imagefile, char *over_image,
				char *down_image, action_cb_t action,
				void *data);

int change_label(tclabel_t *txt, char *text);
tclabel_t* create_label(skin_t *skin, int x, int y, int width, int height,
			int xoff, int yoff, char *text, char *font,
			char *color, short alpha, int scroll,
			action_cb_t action, void *data);

int change_seek_bar(tcseek_bar_t *sb, double position);
tcseek_bar_t *create_seek_bar(skin_t *skin, int x, int y, int sp_x, int sp_y,
			      int ep_x, int ep_y, char *bg, char *indicator,
			      double position, action_cb_t action,
			      void *data);
int enable_seek_bar(tcseek_bar_t *sb);
int disable_seek_bar(tcseek_bar_t *sb);

tcstate_t* create_state(skin_t *skin, int x, int y, int num_states, 
			char **imagefiles, char **states, char *state,
			action_cb_t action, void *data);
int change_state(tcstate_t* st, char *state);

int widget_cmp(const void *, const void *);

#endif /* _TCWIDGETS_H */

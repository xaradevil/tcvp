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
#include <tcvpxtk_tc2.h>
#include <tcconf.h>
#include <tclist.h>


#define TCLABELSTANDARD   (1<<0)
#define TCLABELSCROLLING  (1<<1)
#define TCLABELPINGPONG   (1<<2)
#define TCLABELMANUAL     (1<<3)

typedef struct _tcbackground_t		tcbackground_t;
typedef struct _tcseek_bar_t		tcseek_bar_t;
typedef struct _tcimage_button_t	tcimage_button_t;
typedef struct _tcstate_t		tcstate_t;
typedef struct _tclabel_t		tclabel_t;
typedef struct _tcwidget_common_t	tcwidget_common_t;
typedef union _tcwidget_t		tcwidget_t;

typedef int(*on_xevent_cb_t)(xtk_widget_t *, void *);

#define WIDGET_COMMON_XTK			\
    WIDGET_COMMON;				\
    int width, height;				\
    Pixmap pixmap;				\
    Window win;					\
    int x,y;					\
    int enabled;       				\
    on_xevent_cb_t onclick;			\
    on_xevent_cb_t onpress;			\
    on_xevent_cb_t drag_begin;			\
    on_xevent_cb_t ondrag;			\
    on_xevent_cb_t drag_end;			\
    on_xevent_cb_t enter;			\
    on_xevent_cb_t exit;			\
    on_xevent_cb_t press;			\
    on_xevent_cb_t release;			\
    widget_cb_t repaint;			\
    widget_cb_t destroy

struct _tcwidget_common_t{
    WIDGET_COMMON_XTK;
};

struct _tcbackground_t{
    WIDGET_COMMON_XTK;
    image_info_t *img;
    int transparent;
    Atom xa_rootpmap;
};

struct _tcseek_bar_t{
    WIDGET_COMMON_XTK;
    image_info_t *background;
    image_info_t *indicator;
    int start_x, start_y;
    int end_x, end_y;
    double position;    
};

struct _tcimage_button_t{
    WIDGET_COMMON_XTK;
    image_info_t *img;
    image_info_t *bgimg;
    image_info_t *over_img;
    image_info_t *down_img;
    int pressed;
};

struct _tcstate_t{
    WIDGET_COMMON_XTK;
    int active_state;
    int num_states;
    char **states;
    image_info_t **images;
};

struct _tclabel_t{
    WIDGET_COMMON_XTK;
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
};

union _tcwidget_t {
    int type;
    tcwidget_common_t common;
    tcimage_button_t button;
    tclabel_t label;
    tcbackground_t background;
    tcseek_bar_t seek_bar;
    tcstate_t state;
};

struct _window_t {
    tcbackground_t *background;
    list *widgets;
    Window xw;
    GC wgc, bgc;
    int width, height;
    int enabled;
    int net_wm_support;
    int x, y;
    int mapped;
    void *data;
};

extern list *widget_list, *click_list, *sl_list, *drag_list, *window_list;
extern Display *xd;
extern int xs;
extern Pixmap root;
extern int root_width;
extern int root_height;
extern int depth;
extern int quit;

int widget_onclick(xtk_widget_t *w, void *xe);
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


int widget_cmp(const void *, const void *);

void *x11_event(void *p);
void *scroll_labels(void *p);

#endif /* _TCWIDGETS_H */

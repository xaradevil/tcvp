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

#ifndef _TCWIDGETS_H
#define _TCWIDGETS_H

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/shape.h>
#include <tcvpxtk_tc2.h>
#include <tcconf.h>
#include <tclist.h>

typedef struct _tcbackground_t		tcbackground_t;
typedef struct _tcbox_t			tcbox_t;
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
    int visible;	       			\
    image_info_t *background;			\
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
    xtk_position_t *wp;
    int sx, sy;
};

struct _tcbox_t{
    WIDGET_COMMON_XTK;
    window_t *subwindow;
};

struct _tcseek_bar_t{
    WIDGET_COMMON_XTK;
    image_info_t *indicator;
    image_info_t *standard_img;
    image_info_t *over_img;
    image_info_t *down_img;
    int start_x, start_y;
    int end_x, end_y;
    double position;    
    int state;
};

struct _tcimage_button_t{
    WIDGET_COMMON_XTK;
    image_info_t *img;
    image_info_t *bgimg;
    image_info_t *over_img;
    image_info_t *down_img;
    int pressed;
    Pixmap shapes[3];
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
    int align;
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
    tcbox_t box;
};

struct _window_t {
    tcbackground_t *background;
    tclist_t *widgets;
    Window xw;
    GC wgc, bgc;
    window_t *parent;
    int width, height;
    int enabled;
    int net_wm_support;
    int x, y;
    int mapped;
    int subwindow;
    void *data;
};

extern tclist_t *widget_list, *click_list, *sl_list, *window_list;
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
int draw_window(window_t *win);

int repaint_widgets();
int repaint_window(window_t *win);

int destroy_widget(tcwidget_t *w);

int show_window(window_t *window);
int hide_window(window_t *window);

int alpha_render(unsigned char *src, unsigned char *dest, int width,
		 int height, int depth);
int alpha_render_part(unsigned char *src, unsigned char *dest,
		      int src_x, int src_y, int dest_x, int dest_y,
		      int src_width, int src_height, 
		      int dest_width, int dest_height, int depth);


int widget_cmp(const void *, const void *);

void *x11_event(void *p);
void *scroll_labels(void *p);

extern int shape_window(Window w, image_info_t *im, int op, Pixmap *shape);
extern int merge_shape(window_t *d, Window s, int x, int y);
extern int clear_shape(Window w);

#endif /* _TCWIDGETS_H */

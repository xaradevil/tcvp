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
#include <X11/Xft/Xft.h>
#include <X11/extensions/shape.h>
#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <tcvp_event.h>
#include <tcvpx_tc2.h>
#include <string.h>
#include <unistd.h>

#define STOPPED 0
#define PLAYING 1

static list *widget_list, *bt_list, *sl_list;
static int mapped=1;

static int quit = 0;

static player_t *pl;
static eventq_t qs;
static eventq_t qr;

static Display *xd;
static int xs;
static Window xw;
static GC wgc, bgc;
static Pixmap root;
static int root_width;
static int root_height;
static int depth;
static int s_time;
static int p_state = STOPPED;
static pthread_t xth, eth, sth;

static list *files;
static list_item *flist_curr;
static char *current_file;

typedef struct {
    int x;
    int y;
} pos_t;

#define TCIMAGEBUTTON 0
#define TCLABEL       1
#define TCBACKGROUND  2

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

    uint32_t color;
    char *font;
    double size;
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
    XftColor scroll_xftcolor;
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
};

struct _skin_t {
    pos_t pbg_pos, pcpos, closepos;
    tcimage_button_t *playctl[5], *close;
    tclabel_t *time, *title;
    tcbackground_t *background;
    int width, height;
    char *path;
};

#define MWM_HINTS_DECORATIONS   (1L << 1)
#define PROP_MWM_HINTS_ELEMENTS 5
typedef struct _mwmhints {
    uint32_t flags;
    uint32_t functions;
    uint32_t decorations;
    int32_t  input_mode;
    uint32_t status;
} MWMHints;

static int update_root();
static int update_time(skin_t *);
static int draw_widget(tcwidget_t *);
static int change_label(tclabel_t *txt, char *text);
static int draw_widgets();
static int repaint_widgets();



static int
tcvp_pause(tcwidget_t *p, XEvent *e)
{
    tcvp_event_t *te = tcvp_alloc_event(TCVP_PAUSE);
    eventq_send(qs, te);
    tcfree(te);
    return 0;
}

static int
tcvp_stop(tcwidget_t *p, XEvent *e)
{
    tcvp_event_t *te = tcvp_alloc_event(TCVP_CLOSE);
    p_state = STOPPED;
    eventq_send(qs, te);
    tcfree(te);
    if(p != NULL)
	change_label(p->common.skin->title, "Stopped");

    return 0;
}

static int
tcvp_play(tcwidget_t *p, XEvent *e)
{
    if(current_file != NULL) {
	tcvp_open_event_t *te = tcvp_alloc_event(TCVP_OPEN);
	te->file = current_file;
	eventq_send(qs, te);
	tcfree(te);
	tcvp_pause(NULL, NULL);
    }

    return 0;
}

static int
tcvp_next(tcwidget_t *p, XEvent *e)
{
    int state_tmp = p_state;
    tcvp_stop(NULL, NULL);

    if((current_file = list_next(files, &flist_curr))!=NULL) {
	if(state_tmp == PLAYING) tcvp_play(NULL, NULL);
    } else { 
	p_state = STOPPED;
	change_label(p->common.skin->title, "Stopped");
    }

    return 0;
}

static int
tcvp_previous(tcwidget_t *p, XEvent *e)
{
    int state_tmp = p_state;

    tcvp_stop(NULL, NULL);

    if((current_file = list_prev(files, &flist_curr))!=NULL) {
	if(state_tmp == PLAYING) tcvp_play(NULL, NULL);
    } else {
	p_state = STOPPED;
	change_label(p->common.skin->title, "Stopped");
    }

    return 0;
}

static int
tcvp_close(tcwidget_t *p, XEvent *e)
{
    tcvp_stop(p, NULL);

    tc2_request(TC2_UNLOAD_ALL, 0);

    return 0;
}


static void *
tcvp_event(void *p)
{
    int r = 1;
    skin_t *skin = p;

    while(r){
	tcvp_event_t *te = eventq_recv(qr);
/* 	printf("%d\n", te->type); */
	switch(te->type){
	case TCVP_STATE:
	    switch(te->state.state){
	    case TCVP_STATE_PLAYING:
		p_state = PLAYING;
		break;

	    case TCVP_STATE_ERROR:
		printf("Error opening file.\n");
	    case TCVP_STATE_END:
		s_time = 0;
		update_time(skin);
		if(p_state == PLAYING) {
		    tcvp_next((tcwidget_t *)skin->playctl[4], NULL);
		}
	    }
	    break;

	case TCVP_TIMER:
	    s_time = te->timer.time/1000000;
	    update_time(skin);
	    break;

	case TCVP_LOAD:{
	    char *title = strrchr(te->load.stream->file, '/')+1;
	    if(title == NULL) title = te->load.stream->file;
	    change_label(skin->title, title);
	    break;
	}
	case -1:
	    r = 0;
	    break;
	}
	tcfree(te);
    }
    return NULL;
}


static void *
x11_event(void *p)
{
    int run=1;

    while(run){
        XEvent xe;

        XNextEvent(xd, &xe);
        switch(xe.type){
	case Expose:
	    draw_widgets();
	    break;

	case ConfigureNotify:
	    repaint_widgets();
	    draw_widgets();
	    break;

 	case ButtonPress:
	{
	    list_item *current=NULL;
	    tcwidget_t *bt;

	    while((bt = list_next(bt_list, &current))!=NULL) {
		if(xe.xbutton.window == bt->common.win){
		    if(bt->common.onclick){
			bt->common.onclick(bt, &xe);
		    }
		}
	    }
	    break;
	}

	case MapNotify:	    
	    mapped = 1;
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


static void *
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


static image_info_t*
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


static int
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


static int
update_root()
{
    XImage *img;

    img = XGetImage(xd, RootWindow(xd, xs), 0, 0, root_width, 
		    root_height, AllPlanes, ZPixmap);
    XPutImage(xd, root, bgc, img, 0, 0, 0, 0, root_width, root_height);
    XSync(xd, False);

    XDestroyImage(img);

    return 0;
}


static int
repaint_background(tcwidget_t *w)
{
    XImage *img;
    XWindowAttributes wa;
    Window foo;
    int x, y;

    XGetWindowAttributes(xd, w->background.win, &wa);
    XTranslateCoordinates(xd, w->background.win, RootWindow(xd, xs),
			  wa.x, wa.y, &x, &y, &foo);

    if(x>=0 && y>=0 && x+w->background.width < root_width &&
       y+w->background.height < root_height) {
	img = XGetImage(xd, root, x, y, w->background.width,
			w->background.height, AllPlanes, ZPixmap);
    } else {
	Pixmap tmp_p;
	XImage *tmp_i;
	int tmp_x, tmp_y, tmp_w, tmp_h, xp, yp;

	tmp_p = XCreatePixmap(xd, xw, w->background.width,
			      w->background.height, depth);
	if(x<0) {
	    tmp_x = 0;
	    xp = -x;
	    tmp_w = w->background.width + x;
	} else if(x+w->background.width >= root_width){ 
	    tmp_x = x;
	    xp = 0;
	    tmp_w = w->background.width -
		((x + w->background.width) - root_width);
	} else { 
	    tmp_x = x;
	    xp = 0;
	    tmp_w = w->background.width;
	}

	if(y<0) {
	    tmp_y = 0;
	    yp = -y;
	    tmp_h = w->background.height + y;
	} else if(y+w->background.height >= root_height){
	    tmp_y = y;
	    yp = 0;
	    tmp_h = w->background.height -
		((y + w->background.height) - root_height);
	} else {
	    tmp_y = y;
	    yp = 0;
	    tmp_h = w->background.height;
	}

	tmp_i=XGetImage(xd, root, tmp_x, tmp_y, tmp_w, tmp_h,
			AllPlanes, ZPixmap);
	XPutImage(xd, tmp_p, bgc, tmp_i, 0, 0, xp, yp, tmp_w, tmp_h);
	XSync(xd, False);
	XDestroyImage(tmp_i);
	img = XGetImage(xd, tmp_p, 0, 0, w->background.width,
		       w->background.height, AllPlanes, ZPixmap);
	XSync(xd, False);
	XFreePixmap(xd, tmp_p);
    }

    alpha_render(*w->background.img->data, img->data, w->background.width,
	       w->background.height, depth);

    XPutImage(xd, w->background.pixmap, bgc, img, 0, 0, 0, 0,
	      w->background.width, w->background.height);
    XSync(xd, False);

    XDestroyImage(img);

    return 0;
}

static tcbackground_t*
create_background(skin_t *skin, char *imagefile)
{
    char *data;
    Pixmap maskp;
    tcbackground_t *bg = calloc(sizeof(tcbackground_t), 1);
    int x, y;

    bg->type = TCBACKGROUND;
    bg->x = 0;
    bg->y = 0;
    bg->onclick = NULL;
    bg->repaint = repaint_background;
    bg->skin = skin;
    bg->img = load_image(skin->path, imagefile);
    bg->width = bg->img->width;
    bg->height = bg->img->height;

    bg->pixmap = XCreatePixmap(xd, xw, bg->width,
			       bg->height, depth);
    bg->win = xw;

    data = calloc(bg->width * bg->height,1);
    for(y=0; y<bg->height; y++){
	for(x=0; x<bg->width; x+=8){
	    int i, d=0;
	    for(i=0;i<8;i++){
		d |= (bg->img->data[y][(x+i)*4+3]==0)?0:1<<i;
	    }
	    data[x/8+y*bg->width/8] = d;
	}
    }

    maskp = XCreateBitmapFromData(xd, xw, data, bg->width, bg->height);
    free(data);
    XShapeCombineMask(xd, xw, ShapeBounding, 0, 0, maskp, ShapeSet);
    XSync(xd, False);
    XFreePixmap(xd, maskp);

    list_push(widget_list, bg);

    return bg;
}


static int
repaint_button(tcwidget_t *w)
{
    if(mapped==1){
	XImage *img;
	img = XGetImage(xd, w->button.skin->background->pixmap,
			w->button.x, w->button.y,
			w->button.width, w->button.height,
			AllPlanes, ZPixmap);
	alpha_render(*w->button.img->data, img->data, img->width,
		     img->height, depth);
	XPutImage(xd, w->button.pixmap, bgc, img, 0, 0, 0, 0,
		  w->button.img->width, w->button.img->height);
	XSync(xd, False);
	XDestroyImage(img);
    }
    return 0;
}

static tcimage_button_t*
create_button(skin_t *skin, int x, int y, char *imagefile,
	      onclick_cb_t onclick)
{
    tcimage_button_t *btn = calloc(sizeof(tcimage_button_t), 1);

    btn->type = TCIMAGEBUTTON;
    btn->x = x;
    btn->y = y;
    btn->onclick = onclick;
    btn->repaint = repaint_button;
    btn->skin = skin;
    btn->img = load_image(skin->path, imagefile);
    btn->width = btn->img->width;
    btn->height = btn->img->height;

    btn->win = XCreateWindow(xd, xw, btn->x, btn->y,
			     btn->width, btn->height,
			     0, CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);
    btn->pixmap = XCreatePixmap(xd, xw, btn->width,
				btn->height, depth);
    XSelectInput(xd, btn->win, ButtonPressMask);

    list_push(widget_list, btn);
    if(onclick) list_push(bt_list, btn);

    return btn;
}


static int
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

static int
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

static tclabel_t*
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
    if(onclick) list_push(bt_list, txt);

    return txt;
}


static int
draw_widget(tcwidget_t *w)
{
    if(mapped==1){
	XCopyArea(xd, w->common.pixmap, w->common.win, bgc, 0, 0,
		  w->common.width, w->common.height, 0, 0);
    }
    return 0;
}


static int
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


static int
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


static int
update_time(skin_t *skin)
{
    char text[7];
    
    snprintf(text, 7, "% 3d:%02d", s_time/60, s_time%60);
    change_label(skin->time, text);

    XSync(xd, False);
    
    return 0;
}


static skin_t*
load_skin(char *skinpath)
{
    skin_t *skin=malloc(sizeof(skin_t));

    skin->path = skinpath;

    skin->width = 200;
    skin->height = 20;

    skin->pbg_pos.x = 103;
    skin->pbg_pos.y = 0;
    skin->pcpos.x = 50;
    skin->pcpos.y = 1;
    skin->closepos.x = 187;
    skin->closepos.y = 0;

    return skin;
}


static int
create_ui(skin_t *skin)
{
    skin->background = create_background(skin, "background.png");
    skin->playctl[0] = create_button(skin, skin->pcpos.x+0, skin->pcpos.y+0,
				     "previous.png", tcvp_previous);
    skin->playctl[1] = create_button(skin, skin->pcpos.x+10, skin->pcpos.y+0,
				     "play.png", tcvp_play);
    skin->playctl[2] = create_button(skin, skin->pcpos.x+20, skin->pcpos.y+0,
				     "pause.png", tcvp_pause);
    skin->playctl[3] = create_button(skin, skin->pcpos.x+30, skin->pcpos.y+0,
				     "stop.png", tcvp_stop);
    skin->playctl[4] = create_button(skin, skin->pcpos.x+40, skin->pcpos.y+0,
				     "next.png", tcvp_next);
    skin->playctl[4] = create_button(skin, skin->closepos.x, skin->closepos.y,
				     "close.png", tcvp_close);
    skin->time = create_label(skin, 12, 1, 37, 10, 0, 7, "  0:00", "courier",
			      10.0, 0xFF006030, TCLABELSTANDARD, NULL);
    skin->title = create_label(skin, 12, 10, 176, 10, 0, 7, "Stopped",
			       "courier", 10.0, 0xFF006030,
			       TCLABELPINGPONG, NULL);

    return 0;
}


static int 
init_graphics(skin_t *skin)
{
    Atom prop;
    MWMHints mwmhints;

    bt_list = list_new(TC_LOCK_SLOPPY);
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

    XSync(xd, False);

    return 0;
}


extern int
tcvpx_init(char *p)
{
    char *qname, *qn;
    skin_t *skin;

    files = list_new(TC_LOCK_NONE);
    flist_curr = NULL;

    if(p){
	char *f;
	qname = p;
	
	f = qname + strlen(qname) + 1;
	while(strlen(f)>0) {
	    list_push(files, f);
	    f += strlen(f)+1;
	}
	current_file = list_next(files, &flist_curr);
	
    } else {
	conf_section *cs = conf_new(NULL);
	pl = tcvp_new(cs);
	conf_getvalue(cs, "qname", "%s", &qname);
    }

    qs = eventq_new(NULL);
    qn = alloca(strlen(qname) + 10);
    sprintf(qn, "%s/control", qname);
    eventq_attach(qs, qn, EVENTQ_SEND);

    qr = eventq_new(tcref);
    sprintf(qn, "%s/status", qname);
    eventq_attach(qr, qn, EVENTQ_RECV);
    sprintf(qn, "%s/timer", qname);
    eventq_attach(qr, qn, EVENTQ_RECV);

    skin=load_skin(tcvp_ui_tcvpx_conf_skin);

    init_graphics(skin);
    create_ui(skin);
    update_root();

    XMapWindow (xd, xw);
    XMapSubwindows(xd, xw);

    update_time(skin);
    
    repaint_widgets();
    draw_widgets();

    XSelectInput(xd, xw, ExposureMask | StructureNotifyMask);
    XSync(xd, False);

    pthread_create(&xth, NULL, x11_event, skin);
    pthread_create(&eth, NULL, tcvp_event, skin);
    pthread_create(&sth, NULL, scroll_labels, NULL);

    return 0;
}

extern int
tcvpx_shdn(void)
{
    tcvp_stop(NULL, NULL);

    XDestroyWindow(xd, xw);
    XSync(xd, False);

    quit = 1;

    pthread_join(sth, NULL);
    /* FIXME: join event thread */
/*     pthread_join(eth, NULL); */
    pthread_join(xth, NULL);

    XCloseDisplay(xd);

    eventq_delete(qs);

    if(pl)
	pl->free(pl);

    return 0;
}

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

#define STOPPED 0
#define PLAYING 1

static list *bt_list;

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
static pthread_t xth, eth;

static list *files;
static list_item *flist_curr;
static char *current_file;

typedef struct {
    int x;
    int y;
} pos_t;

typedef struct {
    int x,y;
    image_info_t *img;
    Pixmap pixmap;
    Window win;
    int (*onclick)(void *, XEvent *);
    void *data;
} image_button_t;

typedef struct {
    int x,y;
    uint32_t color;
    char *font;
    double size;
    XftFont *xftfont;
    Pixmap pixmap;
    Window win;
    XftColor xftcolor;
    XftDraw *xftdraw;
    int width, height;
    int xoff, yoff;
} text_t;

typedef struct {
    image_info_t *bg_img, *posbg_img, *posslider_img;
    pos_t pbg_pos;
    image_button_t playctl[5];
    text_t time, title;
    Pixmap bg;
} skin_t;

#define MWM_HINTS_DECORATIONS   (1L << 1)
#define PROP_MWM_HINTS_ELEMENTS 5
typedef struct _mwmhints {
    uint32_t flags;
    uint32_t functions;
    uint32_t decorations;
    int32_t  input_mode;
    uint32_t status;
} MWMHints;

static int update_bg();
static int update_graphics(skin_t *skin);
static int update_time(skin_t *skin);



static int
tcvp_pause(void *p, XEvent *e)
{
    tcvp_event_t *te = tcvp_alloc_event(TCVP_PAUSE);
    eventq_send(qs, te);
    tcfree(te);
    return 0;
}


static int
tcvp_stop(void *p, XEvent *e)
{
    tcvp_event_t *te = tcvp_alloc_event(TCVP_CLOSE);
    p_state = STOPPED;
    eventq_send(qs, te);
    tcfree(te);

    return 0;
}


static int
tcvp_play(void *p, XEvent *e)
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
tcvp_next(void *p, XEvent *e)
{
    int state_tmp = p_state;
    tcvp_stop(NULL, NULL);

    if((current_file = list_next(files, &flist_curr))!=NULL) {
	if(state_tmp == PLAYING) tcvp_play(NULL, NULL);
    } else { 
	p_state = STOPPED;
    }

    return 0;
}


static int
tcvp_previous(void *p, XEvent *e)
{
    int state_tmp = p_state;

    tcvp_stop(NULL, NULL);

    if((current_file = list_prev(files, &flist_curr))!=NULL) {
	if(state_tmp == PLAYING) tcvp_play(NULL, NULL);
    } else {
	p_state = STOPPED;
    }

    return 0;
}


static void *
tcvp_event(void *p)
{
    int r = 1;
    skin_t *skin = p;

    while(r){
	tcvp_event_t *te = eventq_recv(qr);
	switch(te->type){
	case TCVP_STATE:
/* 	    printf("%d\n", te->state.state); */
	    switch(te->state.state){
	    case TCVP_STATE_PLAYING:
		p_state = PLAYING;
		break;

	    case TCVP_STATE_ERROR:
		printf("Error opening file.\n");
	    case TCVP_STATE_END:
		s_time = 0;
		update_time(skin);
		if(p_state == PLAYING)
		    tcvp_next(NULL, NULL);
	    }
	    break;

	case TCVP_TIMER:
	    s_time = te->timer.time/1000000;
	    update_time(skin);
	    break;

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
    skin_t *skin = p;

    while(run){
        XEvent xe;

        XNextEvent(xd, &xe);
        switch(xe.type){
	case Expose:
	case ConfigureNotify:
	    update_graphics(skin);
	    break;

 	case ButtonPress:
	{
	    list_item *current=NULL;
	    image_button_t *bt;

	    while((bt = list_next(bt_list, &current))!=NULL) {
		if(xe.xbutton.window == bt->win){
		    if(bt->onclick){
			bt->onclick(bt->data, &xe);
		    }
		}
	    }
	    break;
	}
       case DestroyNotify:
            run = 0;
            break;
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


static skin_t*
load_skin(char *skinpath)
{
    skin_t *skin=malloc(sizeof(skin_t));

    skin->bg_img = load_image(skinpath, "background.png");
    skin->posbg_img = load_image(skinpath, "posbg.png");
    skin->posslider_img = load_image(skinpath, "posslider.png");

    skin->pbg_pos.x = 103;
    skin->pbg_pos.y = 0;

    skin->playctl[0].x = 0+50;
    skin->playctl[0].y = 0+1;
    skin->playctl[0].img = load_image(skinpath, "previous.png");
    skin->playctl[0].onclick = tcvp_previous;

    skin->playctl[1].x = 10+50;
    skin->playctl[1].y = 0+1;
    skin->playctl[1].img = load_image(skinpath, "play.png");
    skin->playctl[1].onclick = tcvp_play;

    skin->playctl[2].x = 20+50;
    skin->playctl[2].y = 0+1;
    skin->playctl[2].img = load_image(skinpath, "pause.png");
    skin->playctl[2].onclick = tcvp_pause;

    skin->playctl[3].x = 30+50;
    skin->playctl[3].y = 0+1;
    skin->playctl[3].img = load_image(skinpath, "stop.png");
    skin->playctl[3].onclick = tcvp_stop;

    skin->playctl[4].x = 40+50;
    skin->playctl[4].y = 0+1;
    skin->playctl[4].img = load_image(skinpath, "next.png");
    skin->playctl[4].onclick = tcvp_next;

    skin->time.x = 12;
    skin->time.y = 1;
    skin->time.width = 36;
    skin->time.height = 10;
    skin->time.xoff = 0;
    skin->time.yoff = 7;
    skin->time.color = 0xFF006030;
    skin->time.font = "courier";
    skin->time.size = 10.0;

    skin->title.x = 12;
    skin->title.y = 10;
    skin->title.width = 176;
    skin->title.height = 10;    
    skin->title.xoff = 0;
    skin->title.yoff = 7;
    skin->title.color = 0xFF006030;
    skin->title.font = "courier";
    skin->title.size = 10.0;

    return skin;
}


static char*
alpha_render(unsigned char *img, unsigned char *bg, int x_off, int y_off,
	     int width, int height, int bwidth, int bheight, int depth)
{
    int x,y;
    unsigned char *ret = malloc(width*height*4);

    for(y=0;y<height;y++){
	for(x=0;x<width;x++){
	    int pos = (x+y*width)*4;
	    int bpos = (x_off+x+(y+y_off)*bwidth)*4;
	    int b = img[pos+3];
	    int a = 256-b;
	    ret[pos+0] = (bg[bpos+0]*a + img[pos+0]*b)/256;
	    ret[pos+1] = (bg[bpos+1]*a + img[pos+1]*b)/256;
	    ret[pos+2] = (bg[bpos+2]*a + img[pos+2]*b)/256;
	    ret[pos+3] = (bg[bpos+3]*a + img[pos+3]*b)/256;
	}
    }

    return ret;
}


static int
alpha_rend(unsigned char *src, unsigned char *dest, int width,
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
update_bg()
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
add_button(image_button_t *btn)
{
    btn->win = XCreateWindow(xd, xw, btn->x, btn->y,
			     btn->img->width, btn->img->height,
			     0, CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);
    btn->pixmap = XCreatePixmap(xd, xw, btn->img->width,
				btn->img->height, depth);
    XSelectInput(xd, btn->win, ButtonPressMask);
    list_push(bt_list, btn);

    return 0;
}


static int
add_text(text_t *txt)
{
    XRenderColor xrc = {
	.red = (txt->color & 0xff)<<8,
	.green = (txt->color & 0xff00),
	.blue = (txt->color & 0xff0000)>>8,
	.alpha = (txt->color & 0xff000000)>>16
    };

    XftColorAllocValue(xd, DefaultVisual(xd, xs), DefaultColormap(xd, xs),
		       &xrc, &txt->xftcolor);

    txt->xftfont =
	XftFontOpen(xd, xs, XFT_FAMILY, XftTypeString, txt->font,
		    XFT_PIXEL_SIZE, XftTypeDouble, txt->size, NULL);
    if(txt->xftfont==NULL){
	fprintf(stderr, "font \"%s\" with size %f not found\n",
		txt->font, txt->size);
	return 1;
    }

    txt->pixmap = XCreatePixmap(xd, xw, txt->width, txt->height, depth);

    txt->xftdraw = XftDrawCreate(xd, txt->pixmap, DefaultVisual(xd, xs),
				 DefaultColormap(xd, xs));

    txt->win = XCreateWindow(xd, xw, txt->x, txt->y,
			     txt->width, txt->height,
			     0, CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);

/*     XSelectInput(xd, txt->win, ButtonPressMask); */
/*     list_push(bt_list, txt); */

    return 0;
}


static Pixmap
render_text(skin_t *skin, text_t *txt, char *data)
{

    XCopyArea(xd, skin->bg, txt->pixmap, bgc, txt->x, txt->y,
	      txt->width, txt->height, 0, 0);
    XftDrawString8(txt->xftdraw, &txt->xftcolor, txt->xftfont,
		   txt->xoff, txt->yoff, data, strlen(data));

    return 0;
}


static int
render_buttons(skin_t *skin)
{
    list_item *current=NULL;
    image_button_t *bt;
    XImage *img;

    while((bt = list_next(bt_list, &current))!=NULL) {
	img = XGetImage(xd, skin->bg, bt->x, bt->y,
			bt->img->width, bt->img->height,
			AllPlanes, ZPixmap);

	alpha_rend(*bt->img->data, img->data, img->width, img->height, depth);
	XPutImage(xd, bt->pixmap, bgc, img, 0, 0, 0, 0,
		  bt->img->width, bt->img->height);
	XSync(xd, False);
	XDestroyImage(img);
    }

    return 0;
}


static int
draw_buttons(skin_t *skin)
{
    list_item *current=NULL;
    image_button_t *bt;

    while((bt = list_next(bt_list, &current))!=NULL) {
	XCopyArea(xd, bt->pixmap, bt->win, bgc, 0, 0,
		  bt->img->width, bt->img->height, 0, 0);
    }

    return 0;
}


static int
draw_texts(skin_t *skin)
{
    XCopyArea(xd, skin->time.pixmap, skin->time.win, bgc, 0, 0,
	      skin->time.width, skin->time.height, 0, 0);
    return 0;
}


static int
update_time(skin_t *skin)
{
    char text[256];
    
    snprintf(text, 128, "% 3d:%02d", s_time/60, s_time%60);
    render_text(skin, &skin->time, text);
    draw_texts(skin);
    XSync(xd, False);
    
    return 0;
}


static int 
init_graphics(skin_t *skin)
{
    Pixmap maskp;
    Atom prop;
    MWMHints mwmhints;
    char *data;
    int x, y, i;

    bt_list = list_new(TC_LOCK_SLOPPY);

    XInitThreads();
    xd = XOpenDisplay(NULL);

    XSetCloseDownMode (xd, DestroyAll);
    xs = DefaultScreen (xd);

    root_width = XDisplayWidth(xd,xs);
    root_height = XDisplayHeight(xd,xs);
    depth = DefaultDepth(xd, xs);

    xw = XCreateWindow (xd, RootWindow(xd, xs),
			0, 0, 200, 20, 0,
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

    data = calloc(skin->bg_img->width * skin->bg_img->height,1);
    for(y=0; y<skin->bg_img->height; y++){
	for(x=0; x<skin->bg_img->width; x+=8){
	    int i, d=0;
	    for(i=0;i<8;i++){
		d |= (skin->bg_img->data[y][(x+i)*4+3]==0)?0:1<<i;
	    }
	    data[x/8+y*skin->bg_img->width/8] = d;
	}
    }

    maskp = XCreateBitmapFromData(xd, xw, data, skin->bg_img->width,
				  skin->bg_img->height);
    free(data);
    XShapeCombineMask(xd, xw, ShapeBounding, 0, 0, maskp, ShapeSet);

    root = XCreatePixmap(xd, xw, root_width, root_height, depth);
    skin->bg = XCreatePixmap(xd, xw, skin->bg_img->width, skin->bg_img->height, depth);

    for(i=0; i<5; i++){
	add_button(&skin->playctl[i]);
    }
    add_text(&skin->time);

    XSync(xd, False);

    return 0;
}


static int
update_graphics(skin_t *skin)
{
    XImage *img, *bg_img, *bg;
    char *data;
    XWindowAttributes wa;
    Window foo;
    int x, y;

    XGetWindowAttributes(xd, xw, &wa);
    XTranslateCoordinates(xd, xw, RootWindow(xd, xs), wa.x, wa.y, &x, &y, &foo);

    if(x>=0 && y>=0 && x+skin->bg_img->width < root_width &&
       y+skin->bg_img->height < root_height) {
	bg = XGetImage(xd, root, x, y, skin->bg_img->width, skin->bg_img->height,
		       AllPlanes, ZPixmap);
    } else {
	Pixmap tmp_p;
	XImage *tmp_i;
	int tmp_x, tmp_y, tmp_w, tmp_h, xp, yp;

	tmp_p = XCreatePixmap(xd, xw, skin->bg_img->width, skin->bg_img->height, depth);
	if(x<0) {
	    tmp_x = 0;
	    xp = -x;
	    tmp_w = skin->bg_img->width + x;
	} else if(x+skin->bg_img->width >= root_width){ 
	    tmp_x = x;
	    xp = 0;
	    tmp_w = skin->bg_img->width - ((x + skin->bg_img->width) - root_width);
	} else { 
	    tmp_x = x;
	    xp = 0;
	    tmp_w = skin->bg_img->width;
	}

	if(y<0) {
	    tmp_y = 0;
	    yp = -y;
	    tmp_h = skin->bg_img->height + y;
	} else if(y+skin->bg_img->height >= root_height){
	    tmp_y = y;
	    yp = 0;
	    tmp_h = skin->bg_img->height - ((y + skin->bg_img->height) - root_height);
	} else {
	    tmp_y = y;
	    yp = 0;
	    tmp_h = skin->bg_img->height;
	}

	tmp_i=XGetImage(xd, root, tmp_x, tmp_y, tmp_w, tmp_h, AllPlanes, ZPixmap);
	XPutImage(xd, tmp_p, bgc, tmp_i, 0, 0, xp, yp, tmp_w, tmp_h);
	XSync(xd, False);
	XDestroyImage(tmp_i);
	bg = XGetImage(xd, tmp_p, 0, 0, skin->bg_img->width, skin->bg_img->height,
		       AllPlanes, ZPixmap);
	XSync(xd, False);
	XFreePixmap(xd, tmp_p);
    }

    data = alpha_render(*skin->bg_img->data, bg->data, 0, 0, skin->bg_img->width,
			skin->bg_img->height, skin->bg_img->width, skin->bg_img->height,
			depth);
    bg_img = XCreateImage(xd, DefaultVisual(xd, xs), depth,
			  ZPixmap, 0, data, skin->bg_img->width,
			  skin->bg_img->height, BitmapPad(xd), 0);

    XPutImage(xd, skin->bg, bgc, bg_img, 0, 0, 0, 0,
	      skin->bg_img->width, skin->bg_img->height);
    XSync(xd, False);

    data = alpha_render(*skin->posbg_img->data, bg_img->data,
			skin->pbg_pos.x, skin->pbg_pos.y,
			skin->posbg_img->width, skin->posbg_img->height,
			skin->bg_img->width, skin->bg_img->height,
			depth);
    img = XCreateImage(xd, DefaultVisual(xd, xs), depth,
		       ZPixmap, 0, data, skin->posbg_img->width,
		       skin->posbg_img->height, BitmapPad(xd), 0);
    XPutImage(xd, skin->bg, bgc, img, 0, 0, skin->pbg_pos.x, skin->pbg_pos.y,
	      skin->posbg_img->width, skin->posbg_img->height);
    XSync(xd, False);
    XDestroyImage(img);

    XCopyArea(xd, skin->bg, xw, bgc, 0, 0, skin->bg_img->width, skin->bg_img->height, 0, 0);
    XDestroyImage(bg_img);
    XDestroyImage(bg);

    render_buttons(skin);
    draw_buttons(skin);

    update_time(skin);
    draw_texts(skin);

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

    update_bg();

    XMapWindow (xd, xw);
    XMapSubwindows(xd, xw);

    update_time(skin);
    update_graphics(skin);

    XSelectInput(xd, xw, ExposureMask | StructureNotifyMask);
    XSync(xd, False);

    pthread_create(&xth, NULL, x11_event, skin);
    pthread_create(&eth, NULL, tcvp_event, skin);

    return 0;
}

extern int
tcvpx_shdn(void)
{
    eventq_delete(qs);
    if(pl)
	pl->free(pl);

    return 0;
}

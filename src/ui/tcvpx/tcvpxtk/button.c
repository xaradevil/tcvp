/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include "widgets.h"

static void
shape_button(tcimage_button_t *btn, int s)
{
    XShapeCombineMask(xd, btn->win, ShapeBounding, 0, 0,
		      btn->shapes[s], ShapeSet);
}

static int
repaint_button(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if(w->button.window->mapped==1 && w->button.window->enabled == 1){
	XImage *img;
	img = XGetImage(xd, w->button.window->background->pixmap,
			w->button.x, w->button.y,
			w->button.width, w->button.height,
			AllPlanes, ZPixmap);
	if(w->button.background) {
	    alpha_render(*w->button.background->data, img->data, img->width,
			 img->height, depth);
	}
	alpha_render(*w->button.img->data, img->data, img->width,
		     img->height, depth);
	XPutImage(xd, w->button.pixmap, w->background.window->bgc, img,
		  0, 0, 0, 0, w->button.width, w->button.height);
	XSync(xd, False);
	XDestroyImage(img);
    }
    return 0;
}

static void
free_btimg(image_info_t *im)
{
    image_free(im);
    free(im);
}

static int
destroy_button(xtk_widget_t *xw)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    int i;

    free_btimg(w->button.bgimg);
    if(w->button.over_img)
	free_btimg(w->button.over_img);
    if(w->button.down_img)
	free_btimg(w->button.down_img);
    if(w->button.background)
	free_btimg(w->button.background);

    for(i = 0; i < 3; i++)
	if(w->button.shapes[i])
	    XFreePixmap(xd, w->button.shapes[i]);

    return 0;
}


static int
press_button(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    w->button.img = w->button.down_img;
    shape_button(&w->button, 2);
    repaint_button(xw);
    draw_widget(w);
    w->button.pressed = 1;
    return 0;
}


static int
release_button(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if(w->button.img == w->button.down_img) {
	if(w->button.over_img){
	    w->button.img = w->button.over_img;
	    shape_button(&w->button, 1);
	} else {
	    w->button.img = w->button.bgimg;
	    shape_button(&w->button, 0);
	}
    }
    repaint_button(xw);
    draw_widget(w);
    w->button.pressed = 0;
    return 0;
}


static int
enter_button(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    if(w->button.pressed) {
	w->button.img = w->button.down_img;
	shape_button(&w->button, 2);
    } else {
	if(w->button.over_img) {
	    w->button.img = w->button.over_img;
	    shape_button(&w->button, 1);
	}
    }
    XRaiseWindow(xd, w->button.win);
    repaint_button(xw);
    draw_widget(w);
    return 0;
}


static int
exit_button(xtk_widget_t *xw, void *xe)
{
    tcwidget_t *w = (tcwidget_t *)xw;
    w->button.img = w->button.bgimg;
    shape_button(&w->button, 0);
    repaint_button(xw);
    draw_widget(w);
    return 0;
}

extern xtk_widget_t*
create_button(window_t *window, int x, int y, image_info_t *bg,
	      image_info_t *image, image_info_t *over_image,
	      image_info_t *down_image, action_cb_t action, void *data)
{
    tcimage_button_t *btn;
    long emask = 0;

    if(!image) return NULL;

    btn = calloc(sizeof(tcimage_button_t), 1);
    btn->type = TCIMAGEBUTTON;
    btn->x = x;
    btn->y = y;
    btn->repaint = repaint_button;
    btn->destroy = destroy_button;
    btn->window = window;
    btn->background = bg;
    btn->img = btn->bgimg = image;
    btn->width = btn->img->width;
    btn->height = btn->img->height;
    btn->enabled = 1;
    btn->data = data;

    btn->win = XCreateWindow(xd, window->xw, btn->x, btn->y,
			     btn->width, btn->height,
			     0, CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);
    btn->pixmap = XCreatePixmap(xd, window->xw, btn->width,
				btn->height, depth);
    if(bg)
	shape_window(btn->win, bg, ShapeSet, NULL);
    shape_window(btn->win, image, ShapeUnion, btn->shapes);
    if(over_image != NULL) {
	btn->over_img = over_image;
	emask |= EnterWindowMask | LeaveWindowMask;
	btn->enter = enter_button;
	btn->exit = exit_button;
	shape_window(btn->win, over_image, ShapeSet, btn->shapes + 1);
    }
    if(down_image != NULL) {
	btn->down_img = down_image;
	emask |= ButtonPressMask | ButtonReleaseMask;
	btn->press = press_button;
	btn->release = release_button; 
	btn->enter = enter_button;
	btn->exit = exit_button;
	shape_window(btn->win, down_image, ShapeSet, btn->shapes + 2);
    }

    merge_shape(window, btn->win, btn->x, btn->y);
    shape_button(btn, 0);

    list_push(widget_list, btn);
    emask |= ExposureMask;

    if(action){
	btn->action = action;
	btn->onclick = widget_onclick;
	list_push(click_list, btn);
	emask |= ButtonPressMask | ButtonReleaseMask |
	    EnterWindowMask | LeaveWindowMask;
    }

    XSelectInput(xd, btn->win, emask);

    list_push(window->widgets, btn);

    return (xtk_widget_t *) btn;
}

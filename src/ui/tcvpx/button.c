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


static int
repaint_button(tcwidget_t *w)
{
    if(mapped==1 && w->button.skin->enabled == 1){
	XImage *img;
	img = XGetImage(xd, w->button.skin->background->pixmap,
			w->button.x, w->button.y,
			w->button.width, w->button.height,
			AllPlanes, ZPixmap);
	alpha_render(*w->button.img->data, img->data, img->width,
		     img->height, depth);
	XPutImage(xd, w->button.pixmap, w->background.skin->bgc, img,
		  0, 0, 0, 0, w->button.width, w->button.height);
	XSync(xd, False);
	XDestroyImage(img);
    }
    return 0;
}


static int
destroy_button(tcwidget_t *w)
{
    free(*w->button.img->data);
    free(w->button.img->data);
    free(w->button.img);
    return 0;
}


static int
press_button(tcwidget_t *w, XEvent *xe)
{
    w->button.img = w->button.down_img;
    repaint_button(w);
    draw_widget(w);
    return 0;
}


static int
release_button(tcwidget_t *w, XEvent *xe)
{
    w->button.img = w->button.bgimg;
    repaint_button(w);
    draw_widget(w);
    return 0;
}


static int
enter_button(tcwidget_t *w, XEvent *xe)
{
    w->button.img = w->button.over_img;
    repaint_button(w);
    draw_widget(w);
    return 0;
}


static int
exit_button(tcwidget_t *w, XEvent *xe)
{
    w->button.img = w->button.bgimg;
    repaint_button(w);
    draw_widget(w);
    return 0;
}


extern tcimage_button_t*
create_button(skin_t *skin, int x, int y, char *imagefile, char *over_image,
	      char *down_image, action_cb_t action)
{
    tcimage_button_t *btn = calloc(sizeof(tcimage_button_t), 1);
    long emask = 0;

    btn->type = TCIMAGEBUTTON;
    btn->x = x;
    btn->y = y;
    btn->repaint = repaint_button;
    btn->destroy = destroy_button;
    btn->skin = skin;
    btn->img = btn->bgimg = load_image(skin->path, imagefile);
    btn->width = btn->img->width;
    btn->height = btn->img->height;
    btn->enabled = 1;

    if(over_image != NULL) {
	btn->over_img = load_image(skin->path, over_image);
	emask |= EnterWindowMask | LeaveWindowMask;
	btn->enter = enter_button;
	btn->exit = exit_button;
    }
    if(down_image != NULL) {
	btn->down_img = load_image(skin->path, down_image);
	emask |= ButtonPressMask | ButtonReleaseMask;
	btn->press = press_button;
	btn->release = release_button;
    }

    btn->win = XCreateWindow(xd, skin->xw, btn->x, btn->y,
			     btn->width, btn->height,
			     0, CopyFromParent, InputOutput,
			     CopyFromParent, 0, 0);
    btn->pixmap = XCreatePixmap(xd, skin->xw, btn->width,
				btn->height, depth);

    list_push(widget_list, btn);
    emask |= ExposureMask;

    if(action){
	btn->action = action;
	btn->onclick = widget_onclick;
	list_push(click_list, btn);
	emask |= ButtonPressMask;
    }

    XSelectInput(xd, btn->win, emask);

    return btn;
}

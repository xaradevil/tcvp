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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xvlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <tcvp_types.h>
#include <xv_tc2.h>

#define YUY2 0x32595559
#define YV12 0x32315659
#define UYVY 0x59565955

#define FRAMES 64

typedef struct xv_window {
    Display *dpy;
    Window win;
    GC gc;
    XvPortID port;
    int width, height;
    XShmSegmentInfo *shm;
    XvImage **images;
} xv_window_t;

static int
xv_show(video_driver_t *vd, int frame)
{
    xv_window_t *xvw = vd->private;

    XvShmPutImage(xvw->dpy, xvw->port, xvw->win, xvw->gc,
		  xvw->images[frame],
		  0, 0, xvw->width, xvw->height,
		  0, 0, xvw->width, xvw->height,
		  False);
    XSync(xvw->dpy, False);

    return 0;
}

static int
xv_put(video_driver_t *vd, packet_t *pk, int frame)
{
    xv_window_t *xvw = vd->private;
    int i, j;
    XvImage *xi = xvw->images[frame];
    u_char *tmp;

    tmp = pk->data[1];
    pk->data[1] = pk->data[2];
    pk->data[2] = tmp;

    for(i = 0; i < 3; i++){
	for(j = 0; j < xvw->height / (i? 2: 1); j++){
	    memcpy(xi->data + xi->offsets[i] + j * xi->pitches[i],
		   pk->data[i] + j * pk->sizes[i],
		   xi->pitches[i]);
	}
    }

    pk->free(pk);

    return 0;
}

static int
xv_close(video_driver_t *vd)
{
    xv_window_t *xvw = vd->private;
    int i;

    XDestroyWindow(xvw->dpy, xvw->win);

    for(i = 0; i < vd->frames; i++){
	XShmDetach(xvw->dpy, &xvw->shm[i]);
	shmdt(xvw->shm[i].shmaddr);
    }

    XSync(xvw->dpy, False);
    XCloseDisplay(xvw->dpy);

    free(xvw->images);
    free(xvw->shm);
    free(xvw);
    free(vd);

    return 0;
}

extern video_driver_t *
xv_open(video_stream_t *vs, char *display)
{
    video_driver_t *vd;
    xv_window_t *xvw;
    Display *dpy;
    int ver, rev, rb, evb, erb;
    XvAdaptorInfo *xai;
    int na;
    Window win;
    int i;
    GC gc;
    int frames = FRAMES;
    int color_key;
    XEvent xe;

    XInitThreads();

    if((dpy = XOpenDisplay(display)) == NULL)
	return NULL;

    if(XvQueryExtension(dpy, &ver, &rev, &rb, &evb, &erb) != Success)
	return NULL;

    XvQueryAdaptors(dpy, RootWindow(dpy, DefaultScreen(dpy)), &na, &xai);
    if(!na)
	return NULL;

    gc = DefaultGC(dpy, DefaultScreen(dpy));

    win = XCreateWindow(dpy, RootWindow(dpy, DefaultScreen(dpy)),
			0, 0, vs->width, vs->height, 0, CopyFromParent,
			InputOutput, CopyFromParent, 0, NULL);

    xvw = malloc(sizeof(*xvw));
    xvw->dpy = dpy;
    xvw->win = win;
    xvw->gc = gc;
    xvw->port = xai[0].base_id;
    xvw->width = vs->width;
    xvw->height = vs->height;
    xvw->shm = malloc(frames * sizeof(*xvw->shm));
    xvw->images = malloc(frames * sizeof(*xvw->images));

    for(i = 0; i < frames; i++){
	XvImage *xvi;
	XShmSegmentInfo *shm = &xvw->shm[i];

	xvi = XvShmCreateImage(dpy, xvw->port, YV12, NULL,
			       vs->width, vs->height, shm);
	shm->shmid = shmget(IPC_PRIVATE, xvi->data_size, IPC_CREAT | 0777);
	shm->shmaddr = shmat(shm->shmid, 0, 0);
	shm->readOnly = False;
	xvi->data = shm->shmaddr;
	XShmAttach(dpy, shm);
	shmctl(shm->shmid, IPC_RMID, NULL); /* delete now in case we crash */

	xvw->images[i] = xvi;
    }

    vd = malloc(sizeof(*vd));
    vd->frames = frames;
    vd->put_frame = xv_put;
    vd->show_frame = xv_show;
    vd->close = xv_close;
    vd->private = xvw;

    XSelectInput(dpy, win, MapNotify | ExposureMask);
    XMapWindow(dpy, win);
    XSync(dpy, False);
    XNextEvent(dpy, &xe);
    XSelectInput(dpy, win, 0);

    XvGetPortAttribute(dpy, xvw->port,
		       XInternAtom(dpy, "XV_COLORKEY", True), &color_key);
    XSetForeground(dpy, gc, color_key);
    XFillRectangle(dpy, win, gc, 0, 0, xvw->width, xvw->height);
    XvSetPortAttribute(dpy, xvw->port,
		       XInternAtom(dpy, "XV_AUTOPAINT_COLORKEY", True), 0);
    XvSetPortAttribute(dpy, xvw->port,
		       XInternAtom(dpy, "XV_FILTER", True), 0);
    XSync(dpy, False);

    return vd;
}

/**
    Copyright (C) 2003-2004  Michael Ahlberg, Måns Rullgård

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
    int dw, dh;
    XShmSegmentInfo *shm;
    XvImage **images;
    window_manager_t *wm;
    int last_frame;
} xv_window_t;

static int
do_show(xv_window_t *xvw, int frame)
{
    xvw->last_frame = frame;

    XvShmPutImage(xvw->dpy, xvw->port, xvw->win, xvw->gc,
		  xvw->images[frame],
		  0, 0, xvw->width, xvw->height,
		  0, 0, xvw->dw, xvw->dh,
		  False);
    XSync(xvw->dpy, False);

    return 0;
}

static int
xv_show(video_driver_t *vd, int frame)
{
    return do_show(vd->private, frame);
}

static int
xv_get(video_driver_t *vd, int frame, u_char **data, int *strides)
{
    xv_window_t *xvw = vd->private;
    XvImage *xi = xvw->images[frame];
    int i;

    for(i = 0; i < xi->num_planes; i++){
	data[i] = xi->data + xi->offsets[i];
	strides[i] = xi->pitches[i];
    }

    if(frame == xvw->last_frame)
	xvw->last_frame = -1;

    return xi->num_planes;
}

static int
xv_reconf(void *p, int event, int x, int y, int w, int h)
{
    xv_window_t *xvw = p;

    if(event == WM_MOVE){
	xvw->dw = w;
	xvw->dh = h;
    }

    if((event == WM_MOVE || event == WM_SHOW) && xvw->last_frame > -1)
	do_show(xvw, xvw->last_frame);

    return 0;
}

static int
xv_close(video_driver_t *vd)
{
    xv_window_t *xvw = vd->private;
    int i;

    for(i = 0; i < vd->frames; i++){
	XShmDetach(xvw->dpy, &xvw->shm[i]);
	shmdt(xvw->shm[i].shmaddr);
	XFree(xvw->images[i]);
    }

    xvw->wm->close(xvw->wm);

    free(xvw->images);
    free(xvw->shm);
    free(xvw);
    free(vd);

    return 0;
}

extern video_driver_t *
xv_open(video_stream_t *vs, tcconf_section_t *cs)
{
    video_driver_t *vd;
    xv_window_t *xvw;
    Display *dpy;
    int ver, rev, rb, evb, erb;
    XvAdaptorInfo *xai;
    int na;
    Window win;
    int i;
    int frames = tcvp_driver_video_xv_conf_frames?: FRAMES;
    int color_key;
    Atom atm;
    window_manager_t *wm;
    char *display = NULL;
    float dasp = 0;

    if(cs){
	tcconf_getvalue(cs, "video/device", "%s", &display);
	tcconf_getvalue(cs, "video/aspect", "%f", &dasp);
    }

    XInitThreads();

    if((dpy = XOpenDisplay(display)) == NULL)
	return NULL;

    if(XvQueryExtension(dpy, &ver, &rev, &rb, &evb, &erb) != Success){
	return NULL;
    }

    XvQueryAdaptors(dpy, RootWindow(dpy, DefaultScreen(dpy)), &na, &xai);
    if(!na){
	return NULL;
    }

    xvw = calloc(1, sizeof(*xvw));
    xvw->port = xai[0].base_id;
    xvw->width = vs->width;
    xvw->height = vs->height;
    xvw->dw = vs->width;
    xvw->dh = vs->height;
    xvw->shm = malloc(frames * sizeof(*xvw->shm));
    xvw->images = malloc(frames * sizeof(*xvw->images));
    xvw->last_frame = -1;

    XvFreeAdaptorInfo(xai);

    if(dasp > 0 || vs->aspect.num){
	float asp = (float) vs->width / vs->height;
	if(dasp <= 0)
	    dasp = (float) vs->aspect.num / vs->aspect.den;
	if(dasp > asp)
	    xvw->dw = (float) vs->height * dasp;
	else
	    xvw->dh = (float) vs->width / dasp;
    }

    for(i = 0; i < driver_video_xv_conf_attribute_count; i++){
	char *name = driver_video_xv_conf_attribute[i].name;
	int value = driver_video_xv_conf_attribute[i].value;
	if((atm = XInternAtom(dpy, name, True)) != None){
	    XvSetPortAttribute(dpy, xvw->port, atm, value);
	}
    }

    if((atm = XInternAtom(dpy, "XV_COLORKEY", True)) != None){
	if(tcconf_getvalue(cs, "video/color_key", "%i", &color_key) > 0){
	    XvSetPortAttribute(dpy, xvw->port, atm, color_key);
	} else {
	    XvGetPortAttribute(dpy, xvw->port, atm, &color_key);
	    tcconf_setvalue(cs, "video/color_key", "%i", color_key);
	}
    }

    XCloseDisplay(dpy);

    if(!(wm = wm_x11_open(xvw->dw, xvw->dh, xv_reconf, xvw, cs, 0))){
	free(xvw);
	return NULL;
    }
    wm_x11_getwindow(wm, &dpy, &win);

    xvw->dpy = dpy;
    xvw->win = win;
    xvw->gc = DefaultGC(xvw->dpy, DefaultScreen(xvw->dpy));
    xvw->wm = wm;

    for(i = 0; i < frames; i++){
	XvImage *xvi;
	XShmSegmentInfo *shm = &xvw->shm[i];

	xvi = XvShmCreateImage(dpy, xvw->port, YV12, NULL,
			       vs->width, vs->height, shm);
	shm->shmid = shmget(IPC_PRIVATE, xvi->data_size, IPC_CREAT | 0777);
	shm->shmaddr = shmat(shm->shmid, 0, 0);
	shm->readOnly = False;
	xvi->data = shm->shmaddr;
	XShmAttach(xvw->dpy, shm);
	shmctl(shm->shmid, IPC_RMID, NULL); /* delete now in case we crash */

	xvw->images[i] = xvi;
    }

    XSync(xvw->dpy, False);

    vd = calloc(1, sizeof(*vd));
    vd->frames = frames;
    vd->pixel_format = "yv12";
    vd->get_frame = xv_get;
    vd->show_frame = xv_show;
    vd->close = xv_close;
    vd->private = xvw;

    if(display)
	free(display);

    return vd;
}

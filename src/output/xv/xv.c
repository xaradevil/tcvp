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

#define PLAY  1
#define PAUSE 2
#define STOP  3

typedef struct xv_frame {
    XvImage *image;
    uint64_t pts;
} xv_frame_t;

typedef struct xv_window {
    Display *dpy;
    Window win;
    GC gc;
    XvPortID port;
    timer__t *timer;
    int width, height;
    int frames;
    XShmSegmentInfo *shm;
    xv_frame_t *images;
    int state;
    pthread_mutex_t smx;
    pthread_cond_t scd;
    pthread_t thr;
    int head, tail;
    sem_t hsem, tsem;
} xv_window_t;

static void *
xv_play(void *p)
{
    xv_window_t *xvw = p;

#if 0
    int f = 0;
    struct timeval tv;
    uint64_t lu = 0, st;
    uint64_t lpts = 0;

    gettimeofday(&tv, NULL);
    st = tv.tv_sec * 1000000 + tv.tv_usec;
#endif

    while(xvw->state != STOP){
	pthread_mutex_lock(&xvw->smx);
	while(xvw->state == PAUSE){
	    pthread_cond_wait(&xvw->scd, &xvw->smx);
	}
	pthread_mutex_unlock(&xvw->smx);

	sem_wait(&xvw->tsem);

	xvw->timer->wait(xvw->timer, xvw->images[xvw->tail].pts);

#if 0
	if(!(++f & 0x3f)){
	    uint64_t us;
	    gettimeofday(&tv, NULL);
	    us = tv.tv_sec * 1000000 + tv.tv_usec - st;

	    fprintf(stderr, "pts = %li, dpts = %li, us = %li, dus = %li\n",
		    xvw->images[xvw->tail].pts,
		    xvw->images[xvw->tail].pts - lpts,
		    us, us - lu);
	    lu = us;
	    lpts = xvw->images[xvw->tail].pts;
	}
#endif

	XvShmPutImage(xvw->dpy, xvw->port, xvw->win, xvw->gc,
		      xvw->images[xvw->tail].image,
		      0, 0, xvw->width, xvw->height,
		      0, 0, xvw->width, xvw->height,
		      False);
	XSync(xvw->dpy, False);
	sem_post(&xvw->hsem);

	if(++xvw->tail == xvw->frames){
	    xvw->tail = 0;
	}
    }

    return NULL;
}

static int
xv_put(tcvp_pipe_t *p, packet_t *pk)
{
    xv_window_t *xvw = p->private;
    int i, j;
    XvImage *xi = xvw->images[xvw->head].image;
    u_char *tmp;

    tmp = pk->data[1];
    pk->data[1] = pk->data[2];
    pk->data[2] = tmp;

    sem_wait(&xvw->hsem);

    for(i = 0; i < 3; i++){
	for(j = 0; j < xvw->height / (i? 2: 1); j++){
	    memcpy(xi->data + xi->offsets[i] + j * xi->pitches[i],
		   pk->data[i] + j * pk->sizes[i],
		   xi->pitches[i]);
	}
    }

    xvw->images[xvw->head].pts = pk->pts;

    sem_post(&xvw->tsem);

    if(++xvw->head == xvw->frames){
	xvw->head = 0;
    }

    pk->free(pk);

    return 0;
}

static int
xv_start(tcvp_pipe_t *p)
{
    xv_window_t *xvw = p->private;

    pthread_mutex_lock(&xvw->smx);
    xvw->state = PLAY;
    pthread_cond_broadcast(&xvw->scd);
    pthread_mutex_unlock(&xvw->smx);

    return 0;
}

static int
xv_stop(tcvp_pipe_t *p)
{
    xv_window_t *xvw = p->private;

    xvw->state = PAUSE;

    return 0;
}

static int
xv_free(tcvp_pipe_t *p)
{
    xv_window_t *xvw = p->private;
    int i;

    xvw->state = STOP;
    sem_post(&xvw->tsem);
    pthread_join(xvw->thr, NULL);

    XDestroyWindow(xvw->dpy, xvw->win);

    for(i = 0; i < xvw->frames; i++){
	XShmDetach(xvw->dpy, &xvw->shm[i]);
	shmdt(xvw->shm[i].shmaddr);
    }

    XSync(xvw->dpy, False);
    XCloseDisplay(xvw->dpy);

    pthread_mutex_destroy(&xvw->smx);
    pthread_cond_destroy(&xvw->scd);
    sem_destroy(&xvw->hsem);
    sem_destroy(&xvw->tsem);

    free(xvw->images);
    free(xvw->shm);
    free(xvw);
    free(p);

    return 0;
}

extern tcvp_pipe_t *
xv_open(video_stream_t *vs, char *display, timer__t *timer)
{
    tcvp_pipe_t *pipe;
    xv_window_t *xvw;
    Display *dpy;
    int ver, rev, rb, evb, erb;
    XvAdaptorInfo *xai;
    int na;
    Window win;
    int i;
    GC gc;

    XInitThreads();

    if((dpy = XOpenDisplay(display)) == NULL)
	return NULL;

    XvQueryExtension(dpy, &ver, &rev, &rb, &evb, &erb);
/*     printf("Xv %d.%d found.\n", ver, rev); */

    XvQueryAdaptors(dpy, RootWindow(dpy, DefaultScreen(dpy)), &na, &xai);

/*     XvSetPortAttribute(dpy, xai[0].base_id, */
/* 		       XInternAtom(dpy, "XV_FILTER", True), 1); */
    gc = DefaultGC(dpy, DefaultScreen(dpy));

    win = XCreateWindow(dpy, RootWindow(dpy, DefaultScreen(dpy)),
			0, 0, vs->width, vs->height, 0, CopyFromParent,
			InputOutput, CopyFromParent, 0, NULL);

    xvw = malloc(sizeof(*xvw));
    xvw->dpy = dpy;
    xvw->win = win;
    xvw->gc = gc;
    xvw->port = xai[0].base_id;
    xvw->timer = timer;
    xvw->width = vs->width;
    xvw->height = vs->height;
    xvw->frames = FRAMES;
    xvw->shm = malloc(xvw->frames * sizeof(*xvw->shm));
    xvw->images = malloc(xvw->frames * sizeof(*xvw->images));

    for(i = 0; i < xvw->frames; i++){
	XvImage *xvi;
	XShmSegmentInfo *shm = &xvw->shm[i];

	xvi = XvShmCreateImage(dpy, xvw->port, YV12, NULL,
			       vs->width, vs->height, shm);
	shm->shmid = shmget(IPC_PRIVATE, xvi->data_size, IPC_CREAT | 0777);
	shm->shmaddr = shmat(shm->shmid, 0, 0);
	shm->readOnly = False;
	xvi->data = shm->shmaddr;
	XShmAttach(dpy, shm);
/* 	shmdt(shm->shmaddr); */	/* detach now in case we crash */

	xvw->images[i].image = xvi;
    }

    xvw->head = 0;
    xvw->tail = 0;
    sem_init(&xvw->hsem, 0, xvw->frames);
    sem_init(&xvw->tsem, 0, 0);

    pthread_mutex_init(&xvw->smx, NULL);
    pthread_cond_init(&xvw->scd, NULL);
    xvw->state = PAUSE;
    pthread_create(&xvw->thr, NULL, xv_play, xvw);

    pipe = malloc(sizeof(*pipe));
    pipe->input = xv_put;
    pipe->start = xv_start;
    pipe->stop = xv_stop;
    pipe->free = xv_free;
    pipe->private = xvw;

    XMapWindow(dpy, win);
    XSync(dpy, False);

    return pipe;
}

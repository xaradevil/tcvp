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
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <vidix/vidixlib.h>
#include <vidix/fourcc.h>
#include <tcvp_types.h>
#include <vidix_tc2.h>

#define FRAMES 64

#define align(s, a) (((s)+((a)-1)) & ~((a)-1))

typedef struct vidix_window {
    int width, height;
    void *driver;
    int pixel_format;
    vidix_capability_t *caps;
    vidix_playback_t *pbc;
    int frames;
    int vframes;
    int use_dma;
    char **dmabufs;
    int *vfmap;
    int *vfq, vfh, vft, vfc;
    int *ffq, ffh, fft, ffc;
    int lf;
    pthread_mutex_t mx;
    pthread_cond_t cv;
    vidix_dma_t *dma;
    int run;
    pthread_t dmath;
    window_manager_t *wm;
} vx_window_t;

static int
vx_show(video_driver_t *vd, int frame)
{
    vx_window_t *vxw = vd->private;

    if(vxw->use_dma){
	int vframe;
	pthread_mutex_lock(&vxw->mx);
	while(vxw->vfmap[frame] < 0)
	    pthread_cond_wait(&vxw->cv, &vxw->mx);
	vframe = vxw->vfmap[frame];
	vdlPlaybackFrameSelect(vxw->driver, vframe);
	if(vxw->lf > -1){
	    vxw->ffq[vxw->ffh] = vframe;
	    vxw->vfmap[vxw->lf] = -1;
	    vxw->ffc++;
	    if(++vxw->ffh == vxw->vframes)
		vxw->ffh = 0;
	    pthread_cond_broadcast(&vxw->cv);
	}
	vxw->lf = frame;
	pthread_mutex_unlock(&vxw->mx);
    } else {
	vdlPlaybackFrameSelect(vxw->driver, frame);
    }

    return 0;
}

static void *
vx_dmacpy(void *p)
{
    vx_window_t *vxw = p;
    int frame, vframe;

    while(vxw->run){
	pthread_mutex_lock(&vxw->mx);
	while(!(vxw->ffc && vxw->vfc) && vxw->run)
	    pthread_cond_wait(&vxw->cv, &vxw->mx);
	if(!vxw->run){
	    pthread_mutex_unlock(&vxw->mx);
	    break;
	}
	
	frame = vxw->vfq[vxw->vft];
	vframe = vxw->ffq[vxw->fft];

	if(++vxw->vft == vxw->frames)
	    vxw->vft = 0;
	vxw->vfc--;
	if(++vxw->fft == vxw->vframes)
	    vxw->fft = 0;
	vxw->ffc--;
	pthread_mutex_unlock(&vxw->mx);

	vxw->dma->src = vxw->dmabufs[frame];
	vxw->dma->dest_offset = vxw->pbc->offsets[vframe];
	vxw->dma->idx = vframe;
	vdlPlaybackCopyFrame(vxw->driver, vxw->dma);

	pthread_mutex_lock(&vxw->mx);
	vxw->vfmap[frame] = vframe;
	pthread_cond_broadcast(&vxw->cv);
	pthread_mutex_unlock(&vxw->mx);
    }

    return NULL;
}

static int
vx_put(video_driver_t *vd, int frame)
{
    vx_window_t *vxw = vd->private;

    pthread_mutex_lock(&vxw->mx);
    while(vxw->vfc == vxw->frames)
	pthread_cond_wait(&vxw->cv, &vxw->mx);
    vxw->vfq[vxw->vfh] = frame;
    vxw->vfc++;
    if(++vxw->vfh == vxw->frames)
	vxw->vfh = 0;
    pthread_cond_broadcast(&vxw->cv);
    pthread_mutex_unlock(&vxw->mx);

    return 0;
}

static int
vx_flush(video_driver_t *vd)
{
    vx_window_t *vxw = vd->private;
    int i;

    if(vxw->use_dma){
	pthread_mutex_lock(&vxw->mx);
	vxw->vfh = 0;
	vxw->vft = 0;
	vxw->vfc = 0;

	vxw->ffh = 0;
	vxw->fft = 0;
	vxw->ffc = vxw->vframes - 2;
	for(i = 0; i < vxw->vframes; i++)
	    vxw->ffq[i] = i;
	for(i = 0; i < vxw->frames; i++)
	    vxw->vfmap[i] = -1;
	vxw->lf = -1;
	pthread_mutex_unlock(&vxw->mx);
    }

    return 0;
}

static int
vx_get(video_driver_t *vd, int frame, u_char **data, int *strides)
{
    vx_window_t *vxw = vd->private;
    char *fbase;
    int planes = 0;

    if(vxw->use_dma){
	fbase = vxw->dmabufs[frame];
    } else {
	fbase = (char *)vxw->pbc->dga_addr + vxw->pbc->offsets[frame];
    }

    data[0] = fbase + vxw->pbc->offset.y;
    data[1] = fbase + vxw->pbc->offset.u;
    data[2] = fbase + vxw->pbc->offset.v;

    switch(vxw->pixel_format){
    case PIXEL_FORMAT_YV12:
    case PIXEL_FORMAT_I420:
	strides[0] = align(vxw->width, vxw->pbc->dest.pitch.y);
	strides[1] = align(vxw->width/2, vxw->pbc->dest.pitch.u);
	strides[2] = align(vxw->width/2, vxw->pbc->dest.pitch.v);
	planes = 3;
	break;
    case PIXEL_FORMAT_YUY2:
	strides[0] = align(vxw->width*2, vxw->pbc->dest.pitch.y);
	planes = 1;
	break;
    }

    return planes;
}

static int
vx_close(video_driver_t *vd)
{
    vx_window_t *vxw = vd->private;
    int i;

    if(vxw->use_dma){
	pthread_mutex_lock(&vxw->mx);
	vxw->run = 0;
	pthread_cond_broadcast(&vxw->cv);
	pthread_mutex_unlock(&vxw->mx);
	pthread_join(vxw->dmath, NULL);
    }

    vxw->wm->close(vxw->wm);
    vdlPlaybackOff(vxw->driver);
    vdlClose(vxw->driver);

    vdlFreeCapabilityS(vxw->caps);
    vdlFreePlaybackS(vxw->pbc);
    if(vxw->use_dma){
	pthread_mutex_destroy(&vxw->mx);
	pthread_cond_destroy(&vxw->cv);
	vdlFreeDmaS(vxw->dma);
	for(i = 0; i < vxw->frames; i++)
	    free(vxw->dmabufs[i]);
	free(vxw->dmabufs);
	free(vxw->vfmap);
	free(vxw->vfq);
	free(vxw->ffq);
    }

    free(vxw);
    free(vd);

    return 0;
}

static int
vx_reconf(void *p, int event, int x, int y, int w, int h)
{
    vx_window_t *vxw = p;

    switch(event){
    case WM_MOVE:
	vxw->pbc->dest.x = x;
	vxw->pbc->dest.y = y;
	vxw->pbc->dest.w = w;
	vxw->pbc->dest.h = h;
	vdlPlaybackOff(vxw->driver);
	vdlConfigPlayback(vxw->driver, vxw->pbc);
    case WM_SHOW:
	vdlPlaybackOn(vxw->driver);
	break;
    case WM_HIDE:
	vdlPlaybackOff(vxw->driver);
	break;
    }

    return 0;
}

static int fccs[] = {
    [PIXEL_FORMAT_YUY2] = IMGFMT_YUY2,
    [PIXEL_FORMAT_I420] = IMGFMT_I420,
    [PIXEL_FORMAT_YV12] = IMGFMT_YV12
};

extern video_driver_t *
vx_open(video_stream_t *vs, conf_section *cs)
{
    video_driver_t *vd;
    vx_window_t *vxw;
    int i, fmtok = 0, pxf = vs->pixel_format;
    int frames = driver_video_vidix_conf_frames?: FRAMES;
    void *vxdrv;
    char *drvdir;
    vidix_fourcc_t fcc;
    vidix_grkey_t ck;
    int ckey;
    char *drv = NULL;

    if(!(drvdir = driver_video_vidix_conf_driver_dir)){
	fprintf(stderr, "VIDIX: No driver dir.\n");
	return NULL;
    }

    if(driver_video_vidix_conf_driver.name){
	drv = alloca(strlen(driver_video_vidix_conf_driver.name) +
		     strlen(driver_video_vidix_conf_driver.options) + 2);
	sprintf(drv, "%s:%s", driver_video_vidix_conf_driver.name,
		driver_video_vidix_conf_driver.options);
    }

    vxdrv = vdlOpen(drvdir, drv, TYPE_OUTPUT, 0);
    if(!vxdrv){
	fprintf(stderr, "VIDIX: Failed to open driver.\n");
	return NULL;
    }

    fcc.fourcc = fccs[vs->pixel_format];
    fcc.srcw = vs->width;
    fcc.srch = vs->height;
    if(vdlQueryFourcc(vxdrv, &fcc)){
	for(pxf = 1; pxf < sizeof(fccs)/sizeof(fccs[0]); pxf++){
	    fcc.fourcc = fccs[pxf];
	    if(!vdlQueryFourcc(vxdrv, &fcc)){
		fmtok = 1;
		break;
	    }
	}
    }

    if(!fmtok){
	fprintf(stderr, "VIDIX: No supported pixel format found.\n");
	vdlClose(vxdrv);
	return NULL;
    }

    vxw = calloc(1, sizeof(*vxw));
    vxw->width = vs->width;
    vxw->height = vs->height;
    vxw->driver = vxdrv;
    vxw->caps = vdlAllocCapabilityS();
    vxw->pbc = vdlAllocPlaybackS();
    vdlGetCapability(vxdrv, vxw->caps);

    vxw->pixel_format = pxf;
    vxw->pbc->fourcc = fcc.fourcc;
    vxw->pbc->src.w = vs->width;
    vxw->pbc->src.h = vs->height;
    vxw->pbc->dest.x = 0;
    vxw->pbc->dest.y = 0;
    vxw->pbc->dest.w = vs->width;
    vxw->pbc->dest.h = vs->height;
    vxw->pbc->num_frames = frames;

    if(conf_getvalue(cs, "video/color_key", "%i", &ckey) > 0){
	ck.ckey.red = (ckey >> 16) & 0xff;
	ck.ckey.green = (ckey >> 8) & 0xff;
	ck.ckey.blue = ckey & 0xff;
	ck.ckey.op = CKEY_TRUE;
	ck.key_op = KEYS_PUT;
	vdlSetGrKeys(vxdrv, &ck);
    } else {
	vdlGetGrKeys(vxdrv, &ck);
	ckey = (ck.ckey.red << 16) | (ck.ckey.green << 8) | ck.ckey.blue;
	conf_setvalue(cs, "video/color_key", "%i", ckey);
    }

    ck.ckey.op = CKEY_TRUE;
    ck.key_op = KEYS_PUT;
    vdlSetGrKeys(vxdrv, &ck);

    vdlConfigPlayback(vxw->driver, vxw->pbc);

    if(!(vxw->wm = wm_open(vs->width, vs->height, vx_reconf, vxw,
			   cs, WM_ABSCOORD))){
	vdlClose(vxdrv);
	free(vxw);
	return NULL;
    }

    if(vxw->caps->flags & FLAG_SYNC_DMA){
	vxw->dma = vdlAllocDmaS();
	vxw->frames = frames;
	vxw->vframes = vxw->pbc->num_frames;
	pthread_mutex_init(&vxw->mx, NULL);
	pthread_cond_init(&vxw->cv, NULL);
	vxw->vfmap = calloc(frames, sizeof(*vxw->vfmap));
	for(i = 0; i < vxw->frames; i++)
	    vxw->vfmap[i] = -1;
	vxw->vfq = calloc(frames, sizeof(*vxw->vfq));
	vxw->ffq = calloc(vxw->vframes, sizeof(*vxw->ffq));
	vxw->ffc = vxw->vframes - 2;
	for(i = 0; i < vxw->vframes; i++)
	    vxw->ffq[i] = i;
	vxw->lf = -1;

	vxw->dmabufs = malloc(frames * sizeof(*vxw->dmabufs));
	for(i = 0; i < frames; i++)
	    vxw->dmabufs[i] = valloc(vxw->pbc->frame_size);

	vxw->dma->size = vxw->pbc->frame_size;
	vxw->dma->flags = BM_DMA_BLOCK;
	vxw->use_dma = 1;
	vxw->run = 1;
	pthread_create(&vxw->dmath, NULL, vx_dmacpy, vxw);
	fprintf(stderr, "VIDIX: Using DMA.\n");
    } else {
	vxw->frames = vxw->pbc->num_frames;
    }

    fprintf(stderr, "VIDIX: %i frames, %i onboard\n",
	    frames, vxw->pbc->num_frames);

    vd = calloc(1, sizeof(*vd));
    vd->frames = vxw->frames;
    vd->pixel_format = pxf;
    vd->get_frame = vx_get;
    vd->show_frame = vx_show;
    vd->close = vx_close;
    vd->flush = vx_flush;
    vd->private = vxw;
    if(vxw->use_dma)
	vd->put_frame = vx_put;

    return vd;
}

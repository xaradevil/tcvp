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
    char *dmabufs[VID_PLAY_MAXFRAMES];
    int vfmap[VID_PLAY_MAXFRAMES];
    int vframe;
    sem_t hsm, tsm;
    vidix_dma_t *dma;
    window_manager_t *wm;
} vx_window_t;

static int
vx_show(video_driver_t *vd, int frame)
{
    vx_window_t *vxw = vd->private;

    if(vxw->use_dma){
	sem_wait(&vxw->tsm);
	vdlPlaybackFrameSelect(vxw->driver, vxw->vfmap[frame]);
	sem_post(&vxw->hsm);
    } else {
	vdlPlaybackFrameSelect(vxw->driver, frame);
    }

    return 0;
}

static int
vx_put(video_driver_t *vd, int frame)
{
    vx_window_t *vxw = vd->private;

    sem_wait(&vxw->hsm);
    vxw->dma->src = vxw->dmabufs[frame];
    vxw->dma->dest_offset = vxw->pbc->offsets[vxw->vframe];
    vxw->dma->idx = frame;
    vdlPlaybackCopyFrame(vxw->driver, vxw->dma);
    vxw->vfmap[frame] = vxw->vframe;

    if(++vxw->vframe == vxw->vframes)
	vxw->vframe = 0;

    sem_post(&vxw->tsm);

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

    vxw->wm->close(vxw->wm);
    vdlPlaybackOff(vxw->driver);
    vdlClose(vxw->driver);
    for(i = 0; i < VID_PLAY_MAXFRAMES; i++){
	if(vxw->dmabufs[i]){
	    free(vxw->dmabufs[i]);
	}
    }

    vdlFreeCapabilityS(vxw->caps);
    vdlFreePlaybackS(vxw->pbc);
    if(vxw->dma)
	vdlFreeDmaS(vxw->dma);

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

    vdlConfigPlayback(vxw->driver, vxw->pbc);

    if(!(vxw->wm = wm_open(vs->width, vs->height, vx_reconf, vxw,
			   cs, WM_ABSCOORD))){
	vdlClose(vxdrv);
	free(vxw);
	return NULL;
    }

    if(vxw->caps->flags & FLAG_DMA){
	vxw->dma = vdlAllocDmaS();
	vxw->frames = frames;
	vxw->vframes = vxw->pbc->num_frames;
	vxw->vframe = 0;
	sem_init(&vxw->hsm, 0, vxw->vframes - 2);
	sem_init(&vxw->tsm, 0, 0);
	for(i = 0; i < frames; i++){
	    vxw->dmabufs[i] = valloc(vxw->pbc->frame_size);
	}
	vxw->dma->size = vxw->pbc->frame_size;
	vxw->dma->flags = BM_DMA_SYNC;
	vxw->use_dma = 1;
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
    vd->private = vxw;
    if(vxw->use_dma)
	vd->put_frame = vx_put;

    return vd;
}

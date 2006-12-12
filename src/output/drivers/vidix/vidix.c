/**
    Copyright (C) 2003-2006  Michael Ahlberg, Måns Rullgård

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
    u_char **dmabufs;
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
    tcvp_module_t *mod;
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
	    vxw->ffq[vxw->ffh] = vxw->vfmap[vxw->lf];
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
	vxw->ffc = vxw->vframes - 1;
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
    u_char *fbase;
    int planes = 0;

    if(vxw->use_dma){
	fbase = vxw->dmabufs[frame];
    } else {
	fbase = (u_char *)vxw->pbc->dga_addr + vxw->pbc->offsets[frame];
    }

    data[0] = fbase + vxw->pbc->offset.y;
    data[1] = fbase + vxw->pbc->offset.u;
    data[2] = fbase + vxw->pbc->offset.v;

    switch(vxw->pixel_format){
    case IMGFMT_YV12:
    case IMGFMT_I420:
	strides[0] = align(vxw->width, vxw->pbc->dest.pitch.y);
	strides[1] = align(vxw->width, vxw->pbc->dest.pitch.u) / 2;
	strides[2] = align(vxw->width, vxw->pbc->dest.pitch.v) / 2;
	planes = 3;
	break;
    case IMGFMT_YUY2:
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

    vxw->mod->private = NULL;
    tcfree(vxw->mod);

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

extern int
vxe_show(tcvp_module_t *m, tcvp_event_t *te)
{
    vx_window_t *vxw = m->private;
    vdlPlaybackOn(vxw->driver);
    return 0;
}

extern int
vxe_hide(tcvp_module_t *m, tcvp_event_t *te)
{
    vx_window_t *vxw = m->private;
    vdlPlaybackOff(vxw->driver);
    return 0;
}

extern int
vxe_move(tcvp_module_t *m, tcvp_event_t *te)
{
    vx_window_t *vxw = m->private;
    tcvp_wm_move_event_t *me = (tcvp_wm_move_event_t *) te;

    vxw->pbc->dest.x = me->x;
    vxw->pbc->dest.y = me->y;
    vxw->pbc->dest.w = me->w;
    vxw->pbc->dest.h = me->h;
    vdlPlaybackOff(vxw->driver);
    vdlConfigPlayback(vxw->driver, vxw->pbc);
    vdlPlaybackOn(vxw->driver);

    return 0;
}

static struct {
    char *name;
    int code;
} fccs[] = {
    { "yuy2", IMGFMT_YUY2 },
    { "i420", IMGFMT_I420 },
    { "yv12", IMGFMT_YV12 },
    { NULL, -1 }
};

static int
get_fcc(char *name)
{
    int i;

    for(i = 0; fccs[i].name; i++)
	if(!strcmp(name, fccs[i].name))
	    return i;

    return -1;
}

extern video_driver_t *
vx_open(video_stream_t *vs, tcconf_section_t *cs)
{
    video_driver_t *vd;
    vx_window_t *vxw;
    int i, fmtok = 1, pxf;
    int frames = driver_video_vidix_conf_frames?: FRAMES;
    void *vxdrv;
    char *drvdir;
    vidix_fourcc_t fcc;
    vidix_grkey_t ck;
    int ckey;
    char *drv = NULL;
    float dasp = 0;
    int dw, dh;
    char *vc;

    if(!(vc = strstr(vs->codec, "raw-")))
	return NULL;

    vc += 4;
    if((pxf = get_fcc(vc)) < 0)
	return NULL;

    if(cs)
	tcconf_getvalue(cs, "video/aspect", "%f", &dasp);

    if(!(drvdir = driver_video_vidix_conf_driver_dir)){
	tc2_print("VIDIX", TC2_PRINT_ERROR, "No driver dir.\n");
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
	tc2_print("VIDIX", TC2_PRINT_ERROR, "Failed to open driver.\n");
	return NULL;
    }

    fcc.fourcc = fccs[pxf].code;
    fcc.srcw = vs->width;
    fcc.srch = vs->height;
    if(vdlQueryFourcc(vxdrv, &fcc)){
	fmtok = 0;
	for(pxf = 0; pxf < sizeof(fccs)/sizeof(fccs[0]); pxf++){
	    fcc.fourcc = fccs[pxf].code;
	    if(!vdlQueryFourcc(vxdrv, &fcc)){
		fmtok = 1;
		break;
	    }
	}
    }

    if(!fmtok){
	tc2_print("VIDIX", TC2_PRINT_ERROR,
		  "No supported pixel format found.\n");
	vdlClose(vxdrv);
	return NULL;
    }

    tc2_print("VIDIX", TC2_PRINT_DEBUG, "pixel format %s -> %x\n",
	      vc, fccs[pxf].code);

    vxw = calloc(1, sizeof(*vxw));
    vxw->width = vs->width;
    vxw->height = vs->height;
    vxw->driver = vxdrv;
    vxw->caps = vdlAllocCapabilityS();
    vxw->pbc = vdlAllocPlaybackS();
    vdlGetCapability(vxdrv, vxw->caps);

    vxw->pixel_format = fccs[pxf].code;
    vxw->pbc->fourcc = fcc.fourcc;
    vxw->pbc->src.w = vs->width;
    vxw->pbc->src.h = vs->height;
    vxw->pbc->dest.x = 0;
    vxw->pbc->dest.y = 0;
    vxw->pbc->dest.w = vs->width;
    vxw->pbc->dest.h = vs->height;
    vxw->pbc->num_frames = frames;

    dw = vs->width;
    dh = vs->height;

    if(dasp > 0 || vs->aspect.num){
	float asp = (float) vs->width / vs->height;
	if(dasp <= 0)
	    dasp = (float) vs->aspect.num / vs->aspect.den;
	if(dasp > asp)
	    dw = (float) vs->height * dasp;
	else
	    dh = (float) vs->width / dasp;
    }

    vxw->pbc->dest.w = dw;
    vxw->pbc->dest.h = dh;

    if(tcconf_getvalue(cs, "video/color_key", "%i", &ckey) > 0){
	ck.ckey.red = (ckey >> 16) & 0xff;
	ck.ckey.green = (ckey >> 8) & 0xff;
	ck.ckey.blue = ckey & 0xff;
    } else {
	vdlGetGrKeys(vxdrv, &ck);
	ckey = (ck.ckey.red << 16) | (ck.ckey.green << 8) | ck.ckey.blue;
	tcconf_setvalue(cs, "video/color_key", "%i", ckey);
    }

    tc2_print("VIDIX", TC2_PRINT_VERBOSE, "using color key %x\n", ckey);
    ck.ckey.op = CKEY_TRUE;
    ck.key_op = KEYS_PUT;
    vdlSetGrKeys(vxdrv, &ck);

    vdlConfigPlayback(vxw->driver, vxw->pbc);
    tc2_print("VIDIX", TC2_PRINT_DEBUG, "offsets Y %i, U %i, V %i\n",
	      vxw->pbc->offset.y, vxw->pbc->offset.u, vxw->pbc->offset.v);

    vxw->mod = driver_video_vidix_new(cs);
    vxw->mod->private = vxw;
    vxw->mod->init(vxw->mod);

    if(!(vxw->wm = wm_open(dw, dh, cs, WM_ABSCOORD))){
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
	vxw->ffc = vxw->vframes - 1;
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
	tc2_print("VIDIX", TC2_PRINT_VERBOSE, "Using DMA.\n");
    } else {
	vxw->frames = vxw->pbc->num_frames;
    }

    tc2_print("VIDIX", TC2_PRINT_VERBOSE, "%i frames, %i onboard\n",
	      vxw->frames, vxw->pbc->num_frames);

    vd = calloc(1, sizeof(*vd));
    vd->frames = vxw->frames;
    vd->pixel_format = fccs[pxf].name;
    vd->get_frame = vx_get;
    vd->show_frame = vx_show;
    vd->close = vx_close;
    vd->flush = vx_flush;
    vd->private = vxw;
    if(vxw->use_dma)
	vd->put_frame = vx_put;

    return vd;
}

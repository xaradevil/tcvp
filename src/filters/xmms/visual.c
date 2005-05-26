/**
    Copyright (C) 2005  Michael Ahlberg, Måns Rullgård

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

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <tcalloc.h>
#include <tclist.h>
#include <complex.h>
#include <math.h>
#include <fftw3.h>
#include <pthread.h>
#include <tcvp_types.h>
#include <gtk/gtk.h>
#include <xmms/plugin.h>
#include <xmms_tc2.h>

typedef struct xmms_vis_packet {
    uint64_t pts;
    int16_t pcm[2][512];
    int16_t freq[2][256];
} xmms_vis_packet_t;

typedef struct xmms_vis {
    void *handle;
    VisPlugin *vp;
    int count;
    tclist_t *packets;
    xmms_vis_packet_t *nvp;
    double *fpcm;
    fftw_complex *ffreq;
    fftw_plan fft;
    tcvp_timer_t *timer;
    uint64_t pts, dpts, lpts;
    pthread_t vth;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int run;
} xmms_vis_t;

#define min(a, b) ((a)<(b)?(a):(b))

static void *
vis_play(void *p)
{
    xmms_vis_t *xv = p;
    xmms_vis_packet_t *vp;

    tc2_print("XMMS", TC2_PRINT_DEBUG, "vis_play starting\n");

    while(xv->run){
	pthread_mutex_lock(&xv->lock);
	while(xv->run && !tclist_items(xv->packets))
	    pthread_cond_wait(&xv->cond, &xv->lock);
	pthread_mutex_unlock(&xv->lock);

	if(!xv->run)
	    break;

	vp = tclist_shift(xv->packets);
	tc2_print("XMMS", TC2_PRINT_DEBUG+1, "pts %lli\n", vp->pts / 27);
	xv->timer->wait(xv->timer, vp->pts, NULL);
	if(xv->vp->render_pcm && xv->vp->num_pcm_chs_wanted)
	    xv->vp->render_pcm(vp->pcm);
	if(xv->vp->render_freq && xv->vp->num_freq_chs_wanted)
	    xv->vp->render_freq(vp->freq);
	free(vp);
    }

    return NULL;
}

static void
do_fft(xmms_vis_t *xv, xmms_vis_packet_t *vp)
{
    int i, j;

    for(i = 0; i < 2; i++){
	for(j = 0; j < 512; j++)
	    xv->fpcm[j] = (double) vp->pcm[i][j] / 32768.0;
	fftw_execute(xv->fft);
	for(j = 0; j < 256; j++)
	    vp->freq[i][j] = cabs(xv->ffreq[j]) * 64;
    }
}

extern int
vis_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    xmms_vis_t *xv = p->private;

    if(pk->data){
	int16_t *pcm = (int16_t *) pk->data[0];
	int samples = pk->sizes[0] / 2;

	if(pk->flags & TCVP_PKT_FLAG_PTS)
	    xv->pts =
		pk->pts - xv->count * 27000000LL / p->format.audio.sample_rate;

	while(samples > 0){
	    int n = min(samples, 512 - xv->count);
	    int ch = p->format.audio.channels;
	    int i;

	    for(i = 0; i < n; i++){
		xv->nvp->pcm[0][xv->count] = pcm[0];
		xv->nvp->pcm[1][xv->count] = pcm[ch > 1? 1: 0];
		pcm += ch;
		xv->count++;
	    }

	    if(xv->count == 512){
		if(xv->pts - xv->lpts >
		   tcvp_filter_xmms_conf_vis_period * 27000){
		    do_fft(xv, xv->nvp);
		    xv->nvp->pts = xv->pts;
		    xv->lpts = xv->pts;

		    pthread_mutex_lock(&xv->lock);
		    tclist_push(xv->packets, xv->nvp);
		    pthread_cond_broadcast(&xv->cond);
		    pthread_mutex_unlock(&xv->lock);

		    xv->nvp = malloc(sizeof(*xv->nvp));
		}

		xv->pts += xv->dpts;
		xv->count = 0;
	    }

	    samples -= n;
	}
    }

    return p->next->input(p->next, (tcvp_packet_t *) pk);
}

extern int
vis_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    xmms_vis_t *xv = p->private;

    xv->dpts = 512 * 27000000LL / s->audio.sample_rate;
    xv->nvp = malloc(sizeof(*xv->nvp));
    xv->run = 1;

    if(xv->vp->playback_start){
	tc2_print("XMMS", TC2_PRINT_DEBUG, "calling playback_start()\n");
	GDK_THREADS_ENTER();
	xv->vp->playback_start();
	GDK_THREADS_LEAVE();
    }

    pthread_create(&xv->vth, NULL, vis_play, xv);

    return PROBE_OK;
}

static void
vis_free(void *p)
{
    xmms_vis_t *xv = p;

    pthread_mutex_lock(&xv->lock);
    xv->run = 0;
    pthread_cond_broadcast(&xv->cond);
    pthread_mutex_unlock(&xv->lock);
    pthread_join(xv->vth, NULL);

    if(xv->vp->playback_stop){
	tc2_print("XMMS", TC2_PRINT_DEBUG, "calling playback_stop()\n");
	xv->vp->playback_stop();
    }

    if(xv->vp->cleanup){
	tc2_print("XMMS", TC2_PRINT_DEBUG, "calling cleanup()\n");
	GDK_THREADS_ENTER();
	xv->vp->cleanup();
	GDK_THREADS_LEAVE();
    }

    dlclose(xv->handle);

    tclist_destroy(xv->packets, free);
    tcfree(xv->timer);
}

static void
disable_plugin(VisPlugin *vp)
{
}

extern int
vis_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	muxed_stream_t *ms)
{
    xmms_vis_t *xv;
    char *name = NULL;
    char file[1024];
    void *handle;
    int err = 0;
    VisPlugin *(*vpinfo)(void);
    VisPlugin *vp;

    tcconf_getvalue(cs, "name", "%s", &name);
    if(!name){
	tc2_print("XMMS", TC2_PRINT_ERROR, "no plugin name specified\n");
	return -1;
    }

    snprintf(file, sizeof(file), "%s/Visualization/%s.so",
	     tcvp_filter_xmms_conf_plugindir, name);
    handle = dlopen(file, RTLD_NOW);
    if(!handle){
	tc2_print("XMMS", TC2_PRINT_ERROR, "%s\n", dlerror());
	err = -1;
	goto out;
    }

    vpinfo = dlsym(handle, "get_vplugin_info");
    if(!handle){
	tc2_print("XMMS", TC2_PRINT_ERROR, "%s\n", dlerror());
	dlclose(handle);
	err = -1;
	goto out;
    }

    vp = vpinfo();
    tc2_print("XMMS", TC2_PRINT_DEBUG, "%s: %s\n", name, vp->description);
    vp->disable_plugin = disable_plugin;

    if(vp->init){
	tc2_print("XMMS", TC2_PRINT_DEBUG, "calling init()\n");
	GDK_THREADS_ENTER();
	vp->init();
	GDK_THREADS_LEAVE();
    }

    xv = tcallocdz(sizeof(*xv), NULL, vis_free);
    xv->handle = handle;
    xv->vp = vp;
    xv->fpcm = fftw_malloc(512 * sizeof(*xv->fpcm));
    xv->ffreq = fftw_malloc(512 * sizeof(*xv->ffreq));
    xv->fft = fftw_plan_dft_r2c_1d(512, xv->fpcm, xv->ffreq, FFTW_ESTIMATE);
    xv->packets = tclist_new(TC_LOCK_SLOPPY);
    xv->timer = tcref(t);

    p->private = xv;

  out:
    free(name);
    return err;
}

/**
    Copyright (C) 2004  Michael Ahlberg, Måns Rullgård

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
#include <pthread.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <overlay_tc2.h>

typedef struct ovl {
    tcvp_data_packet_t *current, *next;
    tcvp_data_packet_t **fifo;
    int fifosize, fifopos;
    int width, height;
    int *overlays;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} ovl_t;

extern int
ovl_input(tcvp_pipe_t *p, packet_t *pk)
{
    ovl_t *ov = p->private;

    if(ov->overlays[pk->stream]){
	pthread_mutex_lock(&ov->lock);
	if(pk->data){
	    while(ov->next)
		pthread_cond_wait(&ov->cond, &ov->lock);
	    ov->next = pk;
	} else {
	    tcfree(ov->next);
	    ov->next = NULL;
	    pthread_cond_broadcast(&ov->cond);
	    tcfree(pk);
	}
	pthread_mutex_unlock(&ov->lock);
    } else {
	do {
	    if(ov->fifo[ov->fifopos]){
		tcvp_data_packet_t *vp = ov->fifo[ov->fifopos];

		pthread_mutex_lock(&ov->lock);
		if(ov->next && ov->next->pts <= vp->pts){
		    tcfree(ov->current);
		    ov->current = ov->next;
		    ov->next = NULL;
		    tc2_print("OVERLAY", TC2_PRINT_DEBUG,
			      "new overlay, pts %lli, end %lli\n",
			      ov->current->pts, ov->current->dts);
		}
		pthread_cond_broadcast(&ov->cond);
		pthread_mutex_unlock(&ov->lock);

		if(ov->current && ov->current->dts < vp->pts){
		    tcfree(ov->current);
		    ov->current = NULL;
		}

		if(ov->current){
		    tcvp_data_packet_t *opk = ov->current;
		    uint32_t *ovp = (uint32_t *) opk->data[0];
		    int x, y;

		    for(y = 0; y < ov->current->h; y++){
			u_char *yp =
			    vp->data[0] + vp->sizes[0] * (opk->y + y) + opk->x;
			u_char *u = vp->data[1] +
			    vp->sizes[1] * (opk->y + y) / 2 + opk->x / 2;
			u_char *v = vp->data[2] +
			    vp->sizes[2] * (opk->y + y) / 2 + opk->x / 2;
			for(x = 0; x < ov->current->w; x++){
			    int alpha = *ovp >> 24;
			    if(alpha){
				*yp = (*ovp >> 16) & 0xff;
				if(!(y & 1)){
				    *u = (*ovp >> 8) & 0xff;
				    *v = *ovp & 0xff;
				}
			    }
			    ovp++;
			    yp++;
			    if(x & 1){
				u++;
				v++;
			    }
			}
		    }
		}

		p->next->input(p->next, vp);
	    }

	    ov->fifo[ov->fifopos] = pk->data? pk: NULL;
	    if(++ov->fifopos == ov->fifosize)
		ov->fifopos = 0;
	} while(!pk->data && ov->fifopos);

	if(!pk->data)
	    p->next->input(p->next, pk);
    }

    return 0;
}

extern int
ovl_flush(tcvp_pipe_t *p, int drop)
{
    ovl_t *ov = p->private;

    if(drop){
	pthread_mutex_lock(&ov->lock);
	tcfree(ov->next);
	ov->next = NULL;
	tcfree(ov->current);
	ov->current = NULL;
	pthread_cond_broadcast(&ov->cond);
	pthread_mutex_unlock(&ov->lock);
    }

    return 0;
}

extern int
ovl_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    ovl_t *ov = p->private;

    if(s->stream_type == STREAM_TYPE_VIDEO){
	if(ov->width)
	    return PROBE_FAIL;
	ov->width = s->video.width;
	ov->height = s->video.height;
	tc2_print("OVERLAY", TC2_PRINT_DEBUG, "video stream %i\n",
		  s->common.index);
    } else if(s->stream_type == STREAM_TYPE_SUBTITLE){
	ov->overlays[s->common.index] = 1;
	tc2_print("OVERLAY", TC2_PRINT_DEBUG, "overlay stream %i\n",
		  s->common.index);
    } else {
	return PROBE_FAIL;
    }

    return PROBE_OK;
}

static void
ovl_free(void *p)
{
    ovl_t *ov = p;
    int i;

    for(i = 0; i < ov->fifosize; i++)
	tcfree(ov->fifo[i]);
    tcfree(ov->current);
    tcfree(ov->next);

    free(ov->overlays);
    free(ov->fifo);
    pthread_mutex_destroy(&ov->lock);
    pthread_cond_destroy(&ov->cond);
}

extern int
ovl_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	muxed_stream_t *ms)
{
    ovl_t *ov;

    ov = tcallocdz(sizeof(*ov), NULL, ovl_free);
    ov->overlays = calloc(ms->n_streams, sizeof(*ov->overlays));
    ov->fifosize = tcvp_filter_overlay_conf_delay;
    ov->fifo = calloc(ov->fifosize, sizeof(*ov->fifo));
    pthread_mutex_init(&ov->lock, NULL);
    pthread_cond_init(&ov->cond, NULL);

    p->private = ov;
    
    return 0;
}

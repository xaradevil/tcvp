/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <string.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <faad.h>
#include <aac_dec_tc2.h>

typedef struct faad_dec {
    faacDecHandle fd;
    u_char *buf;
    int bufsize;
    int bpos;
    uint64_t pts;
    int ptsp;
} faad_dec_t;

typedef struct faad_packet {
    packet_t pk;
    u_char *data;
    int size;
} faad_packet_t;

#define min(a, b) ((a)<(b)?(a):(b))

static int
decode(tcvp_pipe_t *p, packet_t *pk)
{
    faad_dec_t *ad = p->private;
    faacDecFrameInfo aacframe;
    u_char *samples;
    int size = ad->bpos;
    int flags= 0;
    uint64_t pts = 0;

    samples = faacDecDecode(ad->fd, &aacframe, ad->buf, size);
    size -= aacframe.bytesconsumed;
    if(size > 0)
	memmove(ad->buf, ad->buf + aacframe.bytesconsumed, size);
    ad->ptsp -= aacframe.bytesconsumed;
    if(ad->ptsp < 0 && -ad->ptsp <= aacframe.bytesconsumed){
	flags = TCVP_PKT_FLAG_PTS;
	pts = ad->pts;
    }
    ad->bpos = size;

    if(samples){
	faad_packet_t *opk = tcallocz(sizeof(*opk));
	opk->pk.stream = pk->stream;
	opk->pk.data = &opk->data;
	opk->pk.sizes = &opk->size;
	opk->pk.planes = 1;
	opk->pk.flags = flags;
	opk->pk.pts = pts;
	opk->data = samples;
	opk->size = aacframe.samples * 2;
	p->next->input(p->next, &opk->pk);
    }

    return aacframe.bytesconsumed? 0: -1;
}

static int
faad_input(tcvp_pipe_t *p, packet_t *pk)
{
    faad_dec_t *ad = p->private;
    u_char *data;
    int size;

    if(!pk->data){
	if(ad->bpos)
	    decode(p, pk);
	p->next->input(p->next, pk);
	return 0;
    }

    if(pk->flags & TCVP_PKT_FLAG_PTS && ad->ptsp < 0){
	ad->pts = pk->pts;
	ad->ptsp = ad->bpos;
    }

    data = pk->data[0];
    size = pk->sizes[0];

    while(size > 0){
	int s = min(size, ad->bufsize - ad->bpos);
	memcpy(ad->buf + ad->bpos, data, s);
	ad->bpos += s;
	data += s;
	size -= s;
	if(ad->bpos == ad->bufsize){
	    if(decode(p, pk) < 0)
		break;
	}
    }

    tcfree(pk);
    return 0;
}

static int
faad_flush(tcvp_pipe_t *p, int drop)
{ 
    faad_dec_t *ad = p->private;

    if(drop){
	ad->bpos = 0;
	faacDecPostSeekReset(ad->fd, 0);
    }

    return p->next->flush(p->next, drop);
}

static int
faad_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    faad_dec_t *ad = p->private;
    faacDecConfiguration *fdc;
    u_long srate;
    u_char channels;

    fdc = faacDecGetCurrentConfiguration(ad->fd);
    fdc->outputFormat = FAAD_FMT_16BIT;
    faacDecSetConfiguration(ad->fd, fdc);

    if(faacDecInit(ad->fd, pk->data[0], pk->sizes[0], &srate, &channels) < 0){
	return PROBE_FAIL;
    }

    ad->bufsize = FAAD_MIN_STREAMSIZE * channels;
    ad->buf = malloc(ad->bufsize);

    p->format = *s;
    p->format.common.codec = "audio/pcm-s16" TCVP_ENDIAN;
    p->format.audio.sample_rate = srate;
    p->format.audio.channels = channels;

    return p->next->probe(p->next, NULL, &p->format);
}

static void
faad_free(void *p)
{
    tcvp_pipe_t *tp = p;
    faad_dec_t *ad = tp->private;

    faacDecClose(ad->fd);
    if(ad->buf)
	free(ad->buf);
    free(ad);
}

extern tcvp_pipe_t *
faad_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	 muxed_stream_t *ms)
{
    tcvp_pipe_t *p;
    faad_dec_t *ad;

    ad = calloc(1, sizeof(*ad));
    ad->fd = faacDecOpen();

    p = tcallocdz(sizeof(*p), NULL, faad_free);
    p->format = *s;
    p->format.common.codec = "audio/pcm-s16" TCVP_ENDIAN;
    p->input = faad_input;
    p->flush = faad_flush;
    p->probe = faad_probe;
    p->private = ad;

    return p;
}

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
    tcvp_data_packet_t pk;
    u_char *data;
    int size;
} faad_packet_t;

#define min(a, b) ((a)<(b)?(a):(b))

static int
decode(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
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
	p->next->input(p->next, (tcvp_packet_t *) opk);
    }

    return aacframe.bytesconsumed? 0: -1;
}

extern int
faad_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    faad_dec_t *ad = p->private;
    u_char *data;
    int size;

    if(!pk->data){
	if(ad->bpos)
	    decode(p, pk);
	p->next->input(p->next, (tcvp_packet_t *) pk);
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

extern int
faad_flush(tcvp_pipe_t *p, int drop)
{
    faad_dec_t *ad = p->private;

    if(drop){
	ad->bpos = 0;
	faacDecPostSeekReset(ad->fd, 0);
    }

    return 0;
}

extern int
faad_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    faad_dec_t *ad = p->private;
    faacDecConfiguration *fdc;
    uint32_t srate;
    uint8_t channels;
    int err;

    fdc = faacDecGetCurrentConfiguration(ad->fd);
    fdc->outputFormat = FAAD_FMT_16BIT;
    faacDecSetConfiguration(ad->fd, fdc);

    if(s->common.codec_data){
	err = faacDecInit2(ad->fd, s->common.codec_data,
			   s->common.codec_data_size, &srate, &channels);
    } else {
	err = faacDecInit(ad->fd, pk->data[0], pk->sizes[0],
			  &srate, &channels);
    }

    if(err < 0)
	return PROBE_FAIL;

    ad->bufsize = FAAD_MIN_STREAMSIZE * channels;
    ad->buf = malloc(ad->bufsize);

    p->format.common.codec = "audio/pcm-s16" TCVP_ENDIAN;
    p->format.audio.sample_rate = srate;
    p->format.audio.channels = channels;
    p->format.audio.bit_rate = srate * channels * 16;

    return PROBE_OK;
}

static void
faad_free(void *p)
{
    faad_dec_t *ad = p;

    faacDecClose(ad->fd);
    if(ad->buf)
	free(ad->buf);
}

extern int
faad_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	 muxed_stream_t *ms)
{
    faad_dec_t *ad = tcallocdz(sizeof(*ad), NULL, faad_free);
    ad->fd = faacDecOpen();
    p->format.common.codec = "audio/pcm-s16" TCVP_ENDIAN;
    p->private = ad;

    return 0;
}

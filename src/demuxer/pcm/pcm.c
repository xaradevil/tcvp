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
#include <pcmfmt_tc2.h>
#include <pcmmod.h>

typedef struct pcm {
    url_t *u;
    uint64_t start;
    uint64_t pts;
    uint64_t bytes;
    stream_t s;
    int used;
    int tstamp;
} pcm_t;

typedef struct pcm_packet {
    tcvp_data_packet_t pk;
    int size;
    u_char *data, *buf;
} pcm_packet_t;

static void
pcm_free_pk(void *p)
{
    pcm_packet_t *ep = p;
    free(ep->buf);
}

static tcvp_packet_t *
pcm_packet(muxed_stream_t *ms, int str)
{
    pcm_t *pcm = ms->private;
    pcm_packet_t *ep;
    int size = tcvp_demux_pcm_conf_packet_size * pcm->s.audio.block_align;
    u_char *buf = malloc(size);

    size = pcm->u->read(buf, 1, size, pcm->u);
    if(size <= 0){
	free(buf);
	return NULL;
    }

    ep = tcallocdz(sizeof(*ep), NULL, pcm_free_pk);
    ep->pk.data = &ep->data;
    ep->pk.sizes = &ep->size;
    ep->pk.planes = 1;
    ep->pk.flags = TCVP_PKT_FLAG_PTS;
    ep->buf = buf;

    if(pcm->tstamp){
	pcm->pts = *(uint64_t *) buf;
	buf += 8;
	size -= 8;
    }

    ep->pk.pts = pcm->pts;
    ep->data = buf;
    ep->size = size;

    pcm->bytes += size;
    pcm->pts = pcm->bytes / pcm->s.audio.block_align * 27000000LL /
	pcm->s.audio.sample_rate;

    return (tcvp_packet_t *) ep;
}

static uint64_t
pcm_seek(muxed_stream_t *ms, uint64_t time)
{
    pcm_t *pcm = ms->private;
    uint64_t frame = pcm->s.audio.sample_rate * time / 27000000;
    uint64_t pos = pcm->start + frame * pcm->s.audio.block_align;
    if(pcm->u->seek(pcm->u, pos, SEEK_SET))
	return -1;
    pcm->pts = time;
    pcm->bytes = pos - pcm->start;
    return time;
}

static void
pcm_free(void *p)
{
    muxed_stream_t *ms = p;
    pcm_t *pcm = ms->private;
    if(pcm->u)
	tcfree(pcm->u);
    free(pcm);
}

extern muxed_stream_t *
pcm_open(url_t *u, char *codec, int channels, int srate, int samples,
	 int brate, int bits, char *cd, int cds)
{
    muxed_stream_t *ms;
    pcm_t *pcm;

    pcm = calloc(1, sizeof(*pcm));
    pcm->u = tcref(u);
    pcm->start = u->tell(u);
    pcm->s.stream_type = STREAM_TYPE_AUDIO;
    pcm->s.audio.codec = codec;
    pcm->s.audio.channels = channels;
    pcm->s.audio.sample_rate = srate;
    pcm->s.audio.bit_rate = brate;
    pcm->s.audio.samples = samples;
    pcm->s.audio.block_align = channels * bits / 8;

    if(tcattr_get(u, "tcvp/timestamp")){
	tc2_print("PCM", TC2_PRINT_DEBUG, "timestamps present\n");
	pcm->tstamp = 1;
    }

    ms = tcallocdz(sizeof(*ms), NULL, pcm_free);
    ms->n_streams = 1;
    ms->streams = &pcm->s;
    ms->used_streams = &pcm->used;
    ms->next_packet = pcm_packet;
    ms->seek = pcm_seek;
    ms->time = samples * 27000000LL / srate;
    ms->private = pcm;

    return ms;
}

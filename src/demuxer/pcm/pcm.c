/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

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
    stream_t s;
    int used;
} pcm_t;

typedef struct pcm_packet {
    packet_t pk;
    int size;
    u_char *data;
} pcm_packet_t;

static void
pcm_free_pk(void *p)
{
    pcm_packet_t *ep = p;
    free(ep->data);
}

static packet_t *
pcm_packet(muxed_stream_t *ms, int str)
{
    pcm_t *pcm = ms->private;
    pcm_packet_t *ep;
    int size = 1024;
    u_char *buf = malloc(1024);

    size = pcm->u->read(buf, 1, 1024, pcm->u);
    if(size <= 0){
	free(buf);
	return NULL;
    }

    ep = tcallocdz(sizeof(*ep), NULL, pcm_free_pk);
    ep->pk.data = &ep->data;
    ep->pk.sizes = &ep->size;
    ep->pk.planes = 1;
    ep->data = buf;
    ep->size = size;

    return &ep->pk;
}

static uint64_t
pcm_seek(muxed_stream_t *ms, uint64_t time)
{
    pcm_t *pcm = ms->private;
    uint64_t frame = pcm->s.audio.sample_rate * time / 27000000;
    uint64_t pos = pcm->start + frame * pcm->s.audio.block_align;
    pcm->u->seek(pcm->u, pos, SEEK_SET);
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

    ms = tcallocdz(sizeof(*ms), NULL, pcm_free);
    ms->n_streams = 1;
    ms->streams = &pcm->s;
    ms->used_streams = &pcm->used;
    ms->next_packet = pcm_packet;
    ms->seek = pcm_seek;
    ms->private = pcm;

    return ms;
}
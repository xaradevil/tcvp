/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

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

#include <string.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <lame/lame.h>
#include <lame_tc2.h>
#include <assert.h>

typedef struct lame_enc {
    lame_global_flags *gf;
    int channels;
    uint64_t pts;
    int fpts;
} lame_enc_t;

typedef struct lame_packet {
    packet_t pk;
    int size;
    u_char *data;
} lame_packet_t;

static void
l_free_pk(void *p)
{
    lame_packet_t *lp = p;
    free(lp->data);
}

extern int
l_input(tcvp_pipe_t *p, packet_t *pk)
{
    lame_enc_t *le = p->private;
    u_char *buf;
    int delay = 0;
    int bs;

    if(!pk->data){
	bs = 7200;
	buf = malloc(bs);
	bs = lame_encode_flush(le->gf, buf, bs);
    } else {
	int samples = pk->sizes[0] / le->channels / 2;
	bs = 5 * samples / 4 + 7200;
	buf = malloc(bs);
	delay = lame_get_mf_samples_to_encode(le->gf);
	bs = lame_encode_buffer_interleaved(le->gf, (short *) pk->data[0],
					    samples, buf, bs);
    }

    if(pk->flags & TCVP_PKT_FLAG_PTS){
	uint64_t dp = delay * 27000000LL / p->format.audio.sample_rate;
	le->pts = pk->pts;
	if(dp < le->pts)
	    le->pts -= dp;
	le->fpts = 1;
    }

    if(bs > 0){
	lame_packet_t *lp = tcallocdz(sizeof(*lp), NULL, l_free_pk);
	lp->pk.stream = pk->stream;
	lp->pk.data = &lp->data;
	lp->data = buf;
	lp->pk.sizes = &lp->size;
	lp->size = bs;
	lp->pk.planes = 1;
	if(le->fpts){
	    lp->pk.pts = le->pts;
	    lp->pk.flags |= TCVP_PKT_FLAG_PTS;
	    le->fpts = 0;
	}
	p->next->input(p->next, &lp->pk);
    } else {
	free(buf);
    }

    if(!pk->data)
	p->next->input(p->next, pk);
    else
	tcfree(pk);
    return 0;
}

extern int
l_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    lame_enc_t *le = p->private;

    if(pk)
	tcfree(pk);

    if(!strstr(s->common.codec, "pcm-s16")){
	tc2_print("LAME", TC2_PRINT_ERROR, "unsupported codec %s\n", s->common.codec);
	return PROBE_FAIL;
    }

    le->channels = s->audio.channels;

    lame_set_in_samplerate(le->gf, s->audio.sample_rate);
    lame_set_num_channels(le->gf, s->audio.channels);
    lame_set_bWriteVbrTag(le->gf, 0);
    lame_mp3_tags_fid(le->gf, NULL);
    if(lame_init_params(le->gf) < 0){
	tc2_print("LAME", TC2_PRINT_ERROR, "init failed\n");
	return PROBE_FAIL;
    }

    p->format = *s;
    p->format.audio.codec = "audio/mp3";
    p->format.audio.bit_rate = lame_get_brate(le->gf) * 1000;
    return PROBE_OK;
}

extern int
l_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,
      tcvp_timer_t *t, muxed_stream_t *ms)
{
    lame_enc_t *le = tcallocz(sizeof(*le));
    union { int i; float f; } tmp;

    le->gf = lame_init();

#define lame_set(c, n, f)				\
    if(tcconf_getvalue(cs, #c, "%"#f, &tmp) > 0)	\
	lame_set_##n(le->gf, tmp.f);

    lame_set(scale, scale, f);
    lame_set(samplerate, out_samplerate, i);
    lame_set(quality, quality, i);
    lame_set(mode, mode, i);
    lame_set(bitrate, brate, i);
    lame_set(ratio, compression_ratio, f);
    lame_set(preset, preset, i);

    p->format.common.codec = "audio/mp3";
    p->private = le;

    return 0;
}

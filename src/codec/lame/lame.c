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

#include <string.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <lame/lame.h>
#include <lame_tc2.h>

#define MAX_FRAME_SIZE 16384
#define FRAME_SAMPLES 1152

typedef struct lame_enc {
    lame_global_flags *gf;
    int channels;
    int samplesize;
    uint64_t pts;
    int16_t *buf;
    int samples;
} lame_enc_t;

typedef struct lame_packet {
    packet_t pk;
    int size;
    u_char *data, *buf;
} lame_packet_t;

#define min(a,b) ((a)<(b)?(a):(b))

static void
l_free_pk(void *p)
{
    lame_packet_t *lp = p;
    free(lp->buf);
}

static int
encode_frame(tcvp_pipe_t *p, int16_t *samples)
{
    lame_enc_t *le = p->private;
    int bufsize = MAX_FRAME_SIZE;
    u_char *buf = malloc(bufsize);
    int bs;

    if(samples){
	bs = lame_encode_buffer_interleaved(le->gf, samples, FRAME_SAMPLES,
					    buf, bufsize);
    } else {
	bs = lame_encode_flush(le->gf, buf, bufsize);
    }

    tc2_print("LAME", TC2_PRINT_DEBUG+1, "bs=%i\n", bs);

    if(bs > 0){
	lame_packet_t *lp = tcallocdz(sizeof(*lp), NULL, l_free_pk);
	lp->pk.stream = p->format.common.index;
	lp->pk.data = &lp->data;
	lp->data = buf;
	lp->buf = buf;
	lp->pk.sizes = &lp->size;
	lp->size = bs;
	lp->pk.planes = 1;
	if(le->pts != -1LL){
	    lp->pk.pts = le->pts;
	    lp->pk.flags |= TCVP_PKT_FLAG_PTS;
	    le->pts = -1LL;
	}
	p->next->input(p->next, &lp->pk);
    } else {
	free(buf);
    }

    return 0;
}

extern int
l_input(tcvp_pipe_t *p, packet_t *pk)
{
    lame_enc_t *le = p->private;
    int16_t *data;
    int samples;

    if(!pk->data){
	encode_frame(p, NULL);
	return p->next->input(p->next, pk);
    }

    if(pk->flags & TCVP_PKT_FLAG_PTS && le->pts != -1LL){
	le->pts = pk->pts - (uint64_t) le->samples * 27000000LL /
	    p->format.audio.sample_rate;
    }

    data = (int16_t *) pk->data[0];
    samples = pk->sizes[0] / le->samplesize;

    if(le->samples){
	int rs = min(FRAME_SAMPLES - le->samples, samples);
	memcpy(le->buf + le->samples*le->channels, data, rs * le->samplesize);
	samples -= rs;
	data += rs * le->channels;
	le->samples += rs;

	if(le->samples == FRAME_SAMPLES){
	    encode_frame(p, le->buf);
	    le->samples = 0;
	}
    }

    while(samples >= FRAME_SAMPLES){
	encode_frame(p, data);
	samples -= FRAME_SAMPLES;
	data += FRAME_SAMPLES * le->channels;
    }

    if(samples > 0){
	memcpy(le->buf, data, samples * le->samplesize);
	le->samples = samples;
    }

    tcfree(pk);
    return 0;
}

extern int
l_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    lame_enc_t *le = p->private;

    if(pk)
	tcfree(pk);

    if(strcmp(s->common.codec, "audio/pcm-s16" TCVP_ENDIAN)){
	tc2_print("LAME", TC2_PRINT_ERROR, "unsupported codec %s\n",
		  s->common.codec);
	return PROBE_FAIL;
    }

    if(s->audio.channels > 2){
	tc2_print("LAME", TC2_PRINT_ERROR, "%i channels not supported\n",
		  s->audio.channels);
	return PROBE_FAIL;
    }

    le->channels = s->audio.channels;
    le->samplesize = le->channels * sizeof(int16_t);

    lame_set_in_samplerate(le->gf, s->audio.sample_rate);
    lame_set_num_channels(le->gf, s->audio.channels);
    lame_set_bWriteVbrTag(le->gf, 0);
    lame_mp3_tags_fid(le->gf, NULL);
    if(lame_init_params(le->gf) < 0){
	tc2_print("LAME", TC2_PRINT_ERROR, "init failed\n");
	return PROBE_FAIL;
    }

    le->buf = malloc(FRAME_SAMPLES * le->samplesize);

    p->format.audio.codec = "audio/mp3";
    p->format.audio.bit_rate = lame_get_brate(le->gf) * 1000;

    return PROBE_OK;
}

static void
l_free(void *p)
{
    lame_enc_t *le = p;
    lame_close(le->gf);
    free(le->buf);
}

extern int
l_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,
      tcvp_timer_t *t, muxed_stream_t *ms)
{
    lame_enc_t *le = tcallocdz(sizeof(*le), NULL, l_free);
    union { int i; float f; } tmp;

    le->gf = lame_init();
    le->pts = -1LL;

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

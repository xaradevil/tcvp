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

#include <string.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <lame/lame.h>
#include <lame_tc2.h>

typedef struct lame_enc {
    lame_global_flags *gf;
    int channels;
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

static int
l_input(tcvp_pipe_t *p, packet_t *pk)
{
    lame_enc_t *le = p->private;
    u_char *buf;
    int bs;

    if(!pk->data){
	bs = 7200;
	buf = malloc(bs);
	bs = lame_encode_flush(le->gf, buf, bs);
    } else {
	int samples = pk->sizes[0] / le->channels / 2;
	bs = 5 * samples / 4 + 7200;
	buf = malloc(bs);
	bs = lame_encode_buffer_interleaved(le->gf, (short *) pk->data[0],
					    samples, buf, bs);
    }

    if(bs > 0){
	lame_packet_t *lp = tcallocdz(sizeof(*lp), NULL, l_free_pk);
	lp->pk.stream = pk->stream;
	lp->pk.data = &lp->data;
	lp->data = buf;
	lp->pk.sizes = &lp->size;
	lp->size = bs;
	lp->pk.planes = 1;
	/* FIXME: PTS */
	p->next->input(p->next, &lp->pk);
    } else {
	free(buf);
    }

    tcfree(pk);
    return 0;
}

static int
l_flush(tcvp_pipe_t *p, int drop)
{
    return p->next->flush(p->next, drop);
}

static int
l_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    lame_enc_t *le = p->private;

    if(pk)
	tcfree(pk);

    if(!strstr(s->common.codec, "pcm-s16")){
	fprintf(stderr, "LAME: unsupported codec %s\n", s->common.codec);
	return PROBE_FAIL;
    }

    le->channels = s->audio.channels;

    lame_set_in_samplerate(le->gf, s->audio.sample_rate);
    lame_set_num_channels(le->gf, s->audio.channels);
    lame_set_bWriteVbrTag(le->gf, 0);
    lame_mp3_tags_fid(le->gf, NULL);
    if(lame_init_params(le->gf) < 0){
	fprintf(stderr, "LAME: init failed\n");
	return PROBE_FAIL;
    }

    p->format = *s;
    p->format.audio.codec = "audio/mp3";
    p->format.audio.bit_rate = lame_get_brate(le->gf) * 1000;
    return p->next->probe(p->next, NULL, &p->format);
}

static void
l_free(void *p){
    tcvp_pipe_t *tp = p;
    free(tp->private);
}

extern tcvp_pipe_t *
l_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t, muxed_stream_t *ms)
{
    tcvp_pipe_t *p = tcallocdz(sizeof(*p), NULL, l_free);
    lame_enc_t *le = calloc(1, sizeof(*le));
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

    p->input = l_input;
    p->flush = l_flush;
    p->probe = l_probe;
    p->private = le;

    return p;
}

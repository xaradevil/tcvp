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
#include <stdint.h>
#include <string.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <dts.h>
#include <dts_tc2.h>

#define DTS_LEVEL 1
#define DTS_BIAS 0

#define SYNC_SIZE 14
#define MAX_FRAME_SIZE 18726

typedef struct dts_decode {
    dts_state_t *state;
    sample_t *out;
    sample_t bias;
    level_t level;
    int flags;
    u_char *buf;
    int fsize, fpos;
    uint64_t pts;
    int ptsf;
    int downmix;
} dts_decode_t;

#define min(a,b) ((a)<(b)?(a):(b))

static inline int16_t
convert(sample_t s)
{
    return s * 0x7fff;
}

static inline int
convert_block(sample_t *f, int16_t *s16, int flags)
{
    int i;

    switch(flags){
    case DTS_MONO:
	for(i = 0; i < 256; i++){
	    s16[5*i] = s16[5*i+1] = s16[5*i+2] = s16[5*i+3] = 0;
	    s16[5*i+4] = convert(f[i]);
	}
	return 5;
    case DTS_CHANNEL:
    case DTS_STEREO:
    case DTS_DOLBY:
	for(i = 0; i < 256; i++){
	    s16[2*i] = convert(f[i]);
	    s16[2*i+1] = convert(f[i+256]);
	}
	return 2;
    case DTS_3F:
	for(i = 0; i < 256; i++){
	    s16[5*i] = convert(f[i+256]);
	    s16[5*i+1] = convert(f[i+512]);
	    s16[5*i+2] = s16[5*i+3] = 0;
	    s16[5*i+4] = convert(f[i]);
	}
	return 5;
    case DTS_2F2R:
	for(i = 0; i < 256; i++){
	    s16[4*i] = convert(f[i]);
	    s16[4*i+1] = convert(f[i+256]);
	    s16[4*i+2] = convert(f[i+512]);
	    s16[4*i+3] = convert(f[i+768]);
	}
	return 4;
    case DTS_3F2R:
	for(i = 0; i < 256; i++){
	    s16[5*i] = convert(f[i+256]);
	    s16[5*i+1] = convert(f[i+512]);
	    s16[5*i+2] = convert(f[i+768]);
	    s16[5*i+3] = convert(f[i+1024]);
	    s16[5*i+4] = convert(f[i]);
	}
	return 5;
    case DTS_MONO | DTS_LFE:
	for(i = 0; i < 256; i++){
	    s16[6*i] = s16[6*i+1] = s16[6*i+2] = s16[6*i+3] = 0;
	    s16[6*i+4] = convert(f[i]);
	    s16[6*i+5] = convert(f[i+256]);
	}
	return 6;
    case DTS_CHANNEL | DTS_LFE:
    case DTS_STEREO | DTS_LFE:
    case DTS_DOLBY | DTS_LFE:
	for(i = 0; i < 256; i++){
	    s16[6*i] = convert(f[i]);
	    s16[6*i+1] = convert(f[i+256]);
	    s16[6*i+2] = s16[6*i+3] = s16[6*i+4] = 0;
	    s16[6*i+5] = convert(f[i+512]);
	}
	return 6;
    case DTS_3F | DTS_LFE:
	for(i = 0; i < 256; i++){
	    s16[6*i] = convert(f[i+256]);
	    s16[6*i+1] = convert(f[i+512]);
	    s16[6*i+2] = s16[6*i+3] = 0;
	    s16[6*i+4] = convert(f[i]);
	    s16[6*i+5] = convert(f[i+768]);
	}
	return 6;
    case DTS_2F2R | DTS_LFE:
	for(i = 0; i < 256; i++){
	    s16[6*i] = convert(f[i]);
	    s16[6*i+1] = convert(f[i+256]);
	    s16[6*i+2] = convert(f[i+512]);
	    s16[6*i+3] = convert(f[i+768]);
	    s16[6*i+4] = 0;
	    s16[6*i+5] = convert(f[i+1024]);
	}
	return 6;
    case DTS_3F2R | DTS_LFE:
	for(i = 0; i < 256; i++){
	    s16[6*i] = convert(f[i+256]);
	    s16[6*i+1] = convert(f[i+512]);
	    s16[6*i+2] = convert(f[i+768]);
	    s16[6*i+3] = convert(f[i+1024]);
	    s16[6*i+4] = convert(f[i]);
	    s16[6*i+5] = convert(f[i+1280]);
	}
	return 6;
    default:
	tc2_print("DTS", TC2_PRINT_ERROR, "invalid flags\n");
    }
    return 0;
}

static void
dts_free_pk(void *v)
{
    tcvp_data_packet_t *p = v;
    free(p->sizes);
    free(p->private);
}

static int
decode_frame(tcvp_pipe_t *p, u_char *frame, int str)
{
    dts_decode_t *dts = p->private;
    int i, blocks;

    if(dts->downmix)
	dts->flags = DTS_STEREO;
    dts->level = DTS_LEVEL;

    dts_frame(dts->state, frame, &dts->flags, &dts->level, dts->bias);

    blocks = dts_blocks_num(dts->state);

    if(dts->ptsf > 1)
	dts->pts -= 27000000LL * blocks / p->format.audio.sample_rate;

    for(i = 0; i < blocks; i++){
	int s;
	tcvp_data_packet_t *out;
	int16_t *outbuf = malloc(6 * 256 * sizeof(*outbuf));

	dts_block(dts->state);
	s = convert_block(dts->out, outbuf, dts->flags);

	out = tcallocdz(sizeof(*out), NULL, dts_free_pk);
	out->type = TCVP_PKT_TYPE_DATA;
	out->stream = str;
	out->data = (u_char **) &out->private;
	out->sizes = malloc(sizeof(*out->sizes));
	out->sizes[0] = 256 * s * sizeof(*outbuf);
	out->planes = 1;
	out->private = outbuf;
	out->flags = 0;
	if(dts->ptsf){
	    out->flags |= TCVP_PKT_FLAG_PTS;
	    out->pts = dts->pts;
	    dts->ptsf = 0;
	}

	p->next->input(p->next, (tcvp_packet_t *) out);
    }

    return 0;
}

extern int
dts_decode(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    dts_decode_t *dts = p->private;
    int srate, brate;
    int psize;
    uint8_t *pdata;
    int rs;
    int flength;

    if(!pk->data){
	p->next->input(p->next, (tcvp_packet_t *) pk);
	return 0;
    }

    psize = pk->sizes[0];
    pdata = pk->data[0];

    if(pk->flags & TCVP_PKT_FLAG_PTS){
	dts->pts = pk->pts;
	dts->ptsf = dts->fpos? 2: 1;
    }

    if(dts->fpos > 0){
	if(dts->fpos < SYNC_SIZE){
	    rs = min(SYNC_SIZE - dts->fpos, psize);
	    memcpy(dts->buf + dts->fpos, pdata, rs);
	    dts->fpos += rs;
	    pdata += rs;
	    psize -= rs;
	}

	if(dts->fpos >= SYNC_SIZE){
	    if(!dts->fsize)
		dts->fsize = dts_syncinfo(dts->state, dts->buf, &dts->flags,
					  &srate, &brate, &flength);
	    rs = min(dts->fsize - dts->fpos, psize);
	    memcpy(dts->buf + dts->fpos, pdata, rs);
	    pdata += rs;
	    dts->fpos += rs;
	    psize -= rs;
	}

	if(dts->fpos == dts->fsize){
	    decode_frame(p, dts->buf, pk->stream);
	    dts->fpos = 0;
	    dts->fsize = 0;
	}
    }

    while(psize >= SYNC_SIZE){
	int fsize = 0;
	while(psize >= SYNC_SIZE){
	    fsize = dts_syncinfo(dts->state, pdata, &dts->flags, &srate,
				 &brate, &flength);
	    if(fsize > 0)
		break;
	    psize--;
	    pdata++;
	}
	if(psize < fsize)
	    break;
	decode_frame(p, pdata, pk->stream);
	pdata += fsize;
	psize -= fsize;
    }

    if(psize > 0){
	memcpy(dts->buf, pdata, psize);
	dts->fpos = psize;
    }

    tcfree(pk);
    return 0;
}

extern int
dts_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    dts_decode_t *dts = p->private;
    int flags, srate, bitrate, size = 0, flength;
    int channels;
    int od;

    for(od = 0; od < pk->sizes[0] - SYNC_SIZE; od++){
	size = dts_syncinfo(dts->state, pk->data[0] + od, &flags, &srate,
			    &bitrate, &flength);
	if(size)
	    break;
    }

    if(!size)
	return PROBE_AGAIN;

    s->common.bit_rate = bitrate;

    if(dts->downmix){
	channels = 2;
	dts->flags = DTS_STEREO;
    } else {
	switch(flags & (DTS_CHANNEL_MAX | DTS_LFE)){
	case DTS_CHANNEL:
	case DTS_STEREO:
	case DTS_DOLBY:
	    channels = 2;
	    break;
	case DTS_2F2R:
	    channels = 4;
	    break;
	case DTS_MONO:
	case DTS_3F:
	case DTS_3F2R:
	    channels = 5;
	    break;
	case DTS_MONO | DTS_LFE:
	case DTS_CHANNEL | DTS_LFE:
	case DTS_STEREO | DTS_LFE:
	case DTS_DOLBY | DTS_LFE:
	case DTS_3F | DTS_LFE:
	case DTS_2F2R | DTS_LFE:
	case DTS_3F2R | DTS_LFE:
	    channels = 6;
	    break;
	default:
	    tc2_print("DTS", TC2_PRINT_ERROR, "invalid flags\n");
	    return PROBE_FAIL;
	}
	dts->flags = flags;
    }

    tc2_print("DTS", TC2_PRINT_DEBUG, "flags %x\n", flags);

    p->format.stream_type = STREAM_TYPE_AUDIO;
    p->format.audio.codec = "audio/pcm-s16" TCVP_ENDIAN;
    p->format.audio.sample_rate = srate;
    p->format.audio.channels = channels;
    p->format.audio.bit_rate = srate * channels * 16;

    tcfree(pk);

    return PROBE_OK;
}

static void
dts_free_codec(void *p)
{
    dts_decode_t *dts = p;
    dts_free(dts->state);
    free(dts->buf);
}

extern int
dts_flush(tcvp_pipe_t *p, int drop)
{
    dts_decode_t *dts = p->private;

    if(drop){
	dts->fsize = 0;
	dts->fpos = 0;
	dts->ptsf = 0;
    }

    return 0;
}

extern int
dts_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,
	tcvp_timer_t *t, muxed_stream_t *ms)
{
    dts_decode_t *dts;

    dts = tcallocdz(sizeof(*dts), NULL, dts_free_codec);
    dts->state = dts_init(0);
    dts->out = dts_samples(dts->state);
    dts->level = DTS_LEVEL;
    dts->bias = DTS_BIAS;
    dts->downmix = tcvp_codec_dts_conf_downmix;
    tcconf_getvalue(cs, "downmix", "%i", &dts->downmix);
    if(dts->downmix)
	dts->flags = DTS_STEREO;
    dts->buf = malloc(MAX_FRAME_SIZE);

    p->private = dts;

    return 0;
}

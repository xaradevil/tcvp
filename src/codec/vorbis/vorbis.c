/**
    Copyright (C) 2003-2005  Michael Ahlberg, Måns Rullgård

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
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <vorbis/codec.h>
#include <vorbis_tc2.h>

typedef struct vorbis_context {
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;
    ogg_packet op;
} vorbis_context_t;

typedef struct vorbis_packet {
    tcvp_data_packet_t pk;
    u_char *data;
    int size;
} vorbis_packet_t;

static void
vorbis_free_packet(void *p)
{
    vorbis_packet_t *vp = p;
    free(vp->data);
}

static inline int
float2int(float f)
{
    int i = f * 32768 + 0.5;
    if(i > 32767)
	i = 32767;
    else if(i < -32768)
	i = -32768;
    return i;
}

static inline int
conv(int samples, float **pcm, uint8_t *buf, int channels) {
    int16_t *data = (int16_t *) buf;
    int i, j;

    switch(channels){
    case 2:
	for(i = 0; i < samples; i++){
	    data[0] = float2int(pcm[0][i]);
	    data[1] = float2int(pcm[1][i]);
	    data += 2;
	}
	break;
    case 3:
	for(i = 0; i < samples; i++){
	    data[0] = float2int(pcm[0][i]);
	    data[1] = float2int(pcm[2][i]);
	    data[4] = float2int(pcm[1][i]);
	    data[2] = 0;
	    data[3] = 0;
	    data += 5;
	}
	break;
    case 5:
	for(i = 0; i < samples; i++){
	    data[0] = float2int(pcm[0][i]);
	    data[1] = float2int(pcm[2][i]);
	    data[2] = float2int(pcm[3][i]);
	    data[3] = float2int(pcm[4][i]);
	    data[4] = float2int(pcm[1][i]);
	    data += 5;
	}
	break;
    case 6:
	for(i = 0; i < samples; i++){
	    data[0] = float2int(pcm[0][i]);
	    data[1] = float2int(pcm[2][i]);
	    data[2] = float2int(pcm[3][i]);
	    data[3] = float2int(pcm[4][i]);
	    data[4] = float2int(pcm[1][i]);
	    data[5] = float2int(pcm[5][i]);
	    data += 6;
	}
	break;
    default:
	for(i = 0; i < samples; i++){
	    for(j = 0; j < channels; j++)
		data[j] = float2int(pcm[j][i]);
	    data += channels;
	}
	break;
    }

    return (uint8_t *) data - buf;
}

extern int
vorbis_decode(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    vorbis_packet_t *out;
    vorbis_context_t *vc = p->private;
    int samples, total_bytes;
    u_char *buf, *bp;
    float **pcm;
    int ret;

    if(!pk->data){
	p->next->input(p->next, (tcvp_packet_t *) pk);
	return 0;
    }

    vc->op.packet = pk->data[0];
    vc->op.bytes = pk->sizes[0];

    ret = vorbis_synthesis(&vc->vb, &vc->op);

    if(ret < 0){
	tc2_print("VORBIS", TC2_PRINT_ERROR, "decoder error %i\n", ret);
	return -1;
    }

    if(ret == 0)
	vorbis_synthesis_blockin(&vc->vd, &vc->vb);

    bp = buf = malloc(131072);

    while((samples = vorbis_synthesis_pcmout(&vc->vd, &pcm)) > 0) {
	bp += conv(samples, pcm, bp, vc->vi.channels);
	vorbis_synthesis_read(&vc->vd, samples);
    }

    total_bytes = bp - buf;

    out = tcallocdz(sizeof(*out), NULL, vorbis_free_packet);
    out->pk.stream = pk->stream;
    out->pk.data = &out->data;
    out->pk.sizes = &out->size;
    out->pk.planes = 1;
    out->data = buf;
    out->size = total_bytes;

    if(pk->flags & TCVP_PKT_FLAG_PTS){
	out->pk.flags |= TCVP_PKT_FLAG_PTS;
	out->pk.pts = pk->pts;
    }

    p->next->input(p->next, (tcvp_packet_t *) out);

    tcfree(pk);

    return 0;
}

static int
vorbis_read_header(tcvp_pipe_t *p, stream_t *s)
{
    vorbis_context_t *vc = p->private;
    int size, hs, i;
    ogg_packet op;
    u_char *cdp;

    if(s->common.codec_data_size < 58)
	return -1;

    memset(&op, 0, sizeof(op));

    cdp = s->common.codec_data;
    size = s->common.codec_data_size;

    for(i = 0; i < 3; i++){
	hs = *cdp++ << 8;
	hs += *cdp++;
	size -= 2;

	tc2_print("VORBIS", TC2_PRINT_DEBUG, "header %i size %i\n", i, hs);

	if(hs > size){
	    tc2_print("VORBIS", TC2_PRINT_ERROR,
		      "codec_data too small: %i > %i\n", hs, size);
	    return -1;
	}

	op.packet = cdp;
	op.bytes = hs;
	op.b_o_s = !i;
	vorbis_synthesis_headerin(&vc->vi, &vc->vc, &op);
	op.packetno++;

	cdp += hs;
	size -= hs;
    }

    vorbis_synthesis_init(&vc->vd, &vc->vi);
    vorbis_block_init(&vc->vd, &vc->vb); 

    return 0;
}

extern int
vorbis_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    vorbis_context_t *vc = p->private;

    if(vorbis_read_header(p, s))
	return PROBE_FAIL;

    p->format.audio.codec = "audio/pcm-s16" TCVP_ENDIAN;
    p->format.audio.sample_rate = vc->vi.rate;
    if(vc->vi.channels == 3)
	p->format.audio.channels = 5;
    else
	p->format.audio.channels = vc->vi.channels;
    p->format.audio.bit_rate = vc->vi.rate * p->format.audio.channels * 16;
    s->audio.sample_rate = vc->vi.rate;
    s->audio.channels = vc->vi.channels;
    s->audio.bit_rate = vc->vi.bitrate_nominal;

    return PROBE_OK;
}

static void
vorbis_free_pipe(void *p)
{
    vorbis_context_t *vc = p;

    vorbis_block_clear(&vc->vb);
    vorbis_dsp_clear(&vc->vd);
    vorbis_comment_clear(&vc->vc);
    vorbis_info_clear(&vc->vi);
}

extern int
vorbis_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	   muxed_stream_t *ms)
{
    vorbis_context_t *vc;

    vc = tcallocdz(sizeof(*vc), NULL, vorbis_free_pipe);

    vorbis_info_init(&vc->vi);
    vorbis_comment_init(&vc->vc);

    p->format.audio.codec = "audio/pcm-s16" TCVP_ENDIAN;
    p->private = vc;

    return 0;
}

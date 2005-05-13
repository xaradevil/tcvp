/**
    Copyright (C) 2005  Michael Ahlberg, Måns Rullgård

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
#include <tctypes.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <theora/theora.h>
#include <libtheora_tc2.h>

typedef struct th_context {
    theora_info info;
    theora_state state;
    theora_comment comment;
    ogg_packet op;
    uint64_t pts;
} th_context_t;

typedef struct th_packet {
    tcvp_data_packet_t pk;
    u_char *data[3];
    int strides[3];
} th_packet_t;

extern int
th_decode(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    th_context_t *thc = p->private;
    th_packet_t *out;
    yuv_buffer yuv;

    if(!pk->data){
	p->next->input(p->next, (tcvp_packet_t *) pk);
	return 0;
    }

    if(pk->flags & TCVP_PKT_FLAG_PTS){
	tc2_print("THEORA", TC2_PRINT_DEBUG+1, "in pts %lli\n", pk->pts);
	thc->pts = pk->pts;
    }

    thc->op.packet = pk->data[0];
    thc->op.bytes = pk->sizes[0];

    if(theora_decode_packetin(&thc->state, &thc->op))
	return -1;

    theora_decode_YUVout(&thc->state, &yuv);

    out = tcallocz(sizeof(*out));
    out->pk.stream = pk->stream;
    out->pk.data = out->data;
    out->pk.sizes = out->strides;
    out->pk.planes = 3;

    out->data[0] = yuv.y;
    out->data[1] = yuv.u;
    out->data[2] = yuv.v;

    out->strides[0] = yuv.y_stride;
    out->strides[1] = yuv.uv_stride;
    out->strides[2] = yuv.uv_stride;

    out->pk.flags |= TCVP_PKT_FLAG_PTS;
    out->pk.pts = thc->pts;
    tc2_print("THEORA", TC2_PRINT_DEBUG+1, "out pts %lli\n", out->pk.pts);

    thc->pts += 27000000LL * p->format.video.frame_rate.den /
	p->format.video.frame_rate.num;

    p->next->input(p->next, (tcvp_packet_t *) out);

    tcfree(pk);

    return 0;
}

static int
th_read_header(tcvp_pipe_t *p, stream_t *s)
{
    th_context_t *thc = p->private;
    int size, hs, i;
    ogg_packet op;
    u_char *cdp;

    if(s->common.codec_data_size < 6)
	return -1;

    memset(&op, 0, sizeof(op));

    cdp = s->common.codec_data;
    size = s->common.codec_data_size;

    for(i = 0; i < 3; i++){
	hs = *cdp++ << 8;
	hs += *cdp++;
	size -= 2;

	tc2_print("THEORA", TC2_PRINT_DEBUG, "header %i size %i\n", i, hs);

	if(hs > size){
	    tc2_print("THEORA", TC2_PRINT_ERROR,
		      "codec_data too small: %i > %i\n", hs, size);
	    return -1;
	}

	op.packet = cdp;
	op.bytes = hs;
	op.b_o_s = !i;
	if(theora_decode_header(&thc->info, &thc->comment, &op))
	    return -1;
	op.packetno++;

	cdp += hs;
	size -= hs;
    }

    theora_decode_init(&thc->state, &thc->info);

    return 0;
}

extern int
th_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    if(th_read_header(p, s))
	return PROBE_FAIL;

    p->format.video.codec = "video/raw-i420";

    return PROBE_OK;
}

static void
th_free_pipe(void *p)
{
    th_context_t *thc = p;
    theora_info_clear(&thc->info);
    theora_comment_clear(&thc->comment);
}

extern int
th_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
       muxed_stream_t *ms)
{
    th_context_t *thc;

    thc = tcallocdz(sizeof(*thc), NULL, th_free_pipe);

    theora_info_init(&thc->info);

    p->format.video.codec = "video/raw-i420";
    p->private = thc;

    return 0;
}

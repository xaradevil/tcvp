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
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <x264.h>
#include <x264_tc2.h>

#define X4_BUFSIZE 1048576

typedef struct x4_enc {
    x264_param_t params;
    x264_t *enc;
    x264_picture_t pic;
    u_char *buf;
} x4_enc_t;

typedef struct x4_packet {
    packet_t pk;
    u_char *data;
    int size;
} x4_packet_t;

extern int
x4_encode(tcvp_pipe_t *p, packet_t *pk)
{
    x4_enc_t *x4 = p->private;
    x4_packet_t *ep;
    x264_nal_t *nal;
    int nnal, i;
    u_char *buf;
    int bufsize = X4_BUFSIZE;

    if(!pk->data)
	return p->next->input(p->next, pk);

    x4->pic.img.i_csp = X264_CSP_I420;
    x4->pic.img.i_plane = 3;

    for(i = 0; i < 3; i++){
	x4->pic.img.plane[i] = pk->data[i];
	x4->pic.img.i_stride[i] = pk->sizes[i];
    }

    x4->pic.i_pts = pk->pts;
    x4->pic.i_type = X264_TYPE_AUTO;

    if(x264_encoder_encode(x4->enc, &nal, &nnal, &x4->pic))
	return -1;

    buf = x4->buf;

    for(i = 0; i < nnal; i++){
	int s = x264_nal_encode(buf, &bufsize, 1, nal + i);
	if(s < 0)
	    return -1;
	buf += s;
    }

    ep = tcallocz(sizeof(*ep));
    ep->pk.stream = pk->stream;
    ep->pk.data = &ep->data;
    ep->pk.sizes = &ep->size;
    ep->pk.planes = 1;
    ep->pk.flags = TCVP_PKT_FLAG_PTS;
    if(x4->pic.i_type == X264_TYPE_I)
	ep->pk.flags |= TCVP_PKT_FLAG_KEY;
    ep->pk.pts = x4->pic.i_pts;
    ep->data = x4->buf;
    ep->size = buf - x4->buf;
    p->next->input(p->next, &ep->pk);

    tcfree(pk);
    return 0;
}

extern int
x4_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    x4_enc_t *x4 = p->private;

    if(strcmp(s->common.codec, "video/raw-i420"))
	return PROBE_FAIL;

    p->format.common.codec = "video/h264";
    p->format.common.bit_rate = 1000000;

    x4->params.i_width = s->video.width;
    x4->params.i_height = s->video.height;
    x4->params.i_csp = X264_CSP_I420;
    x4->params.vui.i_sar_width = s->video.height * s->video.aspect.num;
    x4->params.vui.i_sar_height = s->video.width * s->video.aspect.den;
    x4->params.f_fps =
	(float) s->video.frame_rate.num / s->video.frame_rate.den;

    x4->enc = x264_encoder_open(&x4->params);
    if(!x4->enc)
	return PROBE_FAIL;

    return PROBE_OK;
}

static void
x4_free(void *p)
{
    x4_enc_t *x4 = p;

    if(x4->enc)
	x264_encoder_close(x4->enc);
    free(x4->buf);
}

extern int
x4_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,
       tcvp_timer_t *t, muxed_stream_t *ms)
{
    x4_enc_t *x4;

    x4 = tcallocdz(sizeof(*x4), NULL, x4_free);
    x264_param_default(&x4->params);
    x4->buf = malloc(X4_BUFSIZE);

    p->format = *s;
    p->format.common.codec = "video/h264";
    p->private = x4;

    return 0;
}

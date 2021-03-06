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
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
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
    int pts_valid;
} x4_enc_t;

typedef struct x4_packet {
    tcvp_data_packet_t pk;
    u_char *data, *buf;
    int size;
} x4_packet_t;

static void
x4_log(void *p, int level, const char *fmt, va_list args)
{
    static const int level_map[] = {
        [X264_LOG_ERROR]   = TC2_PRINT_ERROR,
        [X264_LOG_WARNING] = TC2_PRINT_WARNING,
        [X264_LOG_INFO]    = TC2_PRINT_INFO,
        [X264_LOG_DEBUG]   = TC2_PRINT_DEBUG + 1
    };

    if(level < 0 || level > X264_LOG_DEBUG)
        return;

    tc2_printv("X264", level_map[level], fmt, args);
}


static void
x4_free_pk(void *p)
{
    x4_packet_t *xp = p;
    free(xp->buf);
}

static int
encode_nals(u_char *buf, int size, x264_nal_t *nals, int nnal)
{
    u_char *p = buf;
    int i;

    for(i = 0; i < nnal; i++){
        int s = x264_nal_encode(p, &size, 1, nals + i);
        if(s < 0)
            return -1;
        p += s;
    }

    return p - buf;
}

extern int
x4_encode(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    x4_enc_t *x4 = p->private;
    x4_packet_t *ep;
    x264_nal_t *nal;
    int nnal, i;
    u_char *buf;
    int bufsize = X4_BUFSIZE;
    x264_picture_t pic_out;

    if(!pk->data)
        return p->next->input(p->next, (tcvp_packet_t *) pk);

    x4->pic.img.i_csp = X264_CSP_I420;
    x4->pic.img.i_plane = 3;

    for(i = 0; i < 3; i++){
        x4->pic.img.plane[i] = pk->data[i];
        x4->pic.img.i_stride[i] = pk->sizes[i];
    }

    if(pk->flags & TCVP_PKT_FLAG_PTS){
        x4->pic.i_pts = pk->pts;
        x4->pts_valid = 1;
    }

    if(pk->flags & TCVP_PKT_FLAG_DISCONT){
        x4->pic.i_type = X264_TYPE_IDR;
    } else {
        x4->pic.i_type = X264_TYPE_AUTO;
    }

    if(x264_encoder_encode(x4->enc, &nal, &nnal, &x4->pic, &pic_out))
        return -1;

    buf = malloc(bufsize);
    bufsize = encode_nals(buf, bufsize, nal, nnal);
    if(bufsize < 0)
        return -1;

    ep = tcallocdz(sizeof(*ep), NULL, x4_free_pk);
    ep->pk.stream = pk->stream;
    ep->pk.data = &ep->data;
    ep->pk.sizes = &ep->size;
    ep->pk.planes = 1;
    ep->pk.flags = 0;
    if(x4->pts_valid){
        ep->pk.flags |= TCVP_PKT_FLAG_PTS;
        ep->pk.pts = pic_out.i_pts;
    }
    if(pic_out.i_type == X264_TYPE_I || pic_out.i_type == X264_TYPE_IDR)
        ep->pk.flags |= TCVP_PKT_FLAG_KEY;
    ep->data = buf;
    ep->buf = buf;
    ep->size = bufsize;
    p->next->input(p->next, (tcvp_packet_t *) ep);

    tcfree(pk);
    return 0;
}

extern int
x4_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    x4_enc_t *x4 = p->private;

    if(strcmp(s->common.codec, "video/raw-i420"))
        return PROBE_FAIL;

    p->format.common.codec = "video/h264";
    p->format.common.bit_rate = x4->params.rc.i_bitrate * 1000;

    x4->params.i_width = s->video.width;
    x4->params.i_height = s->video.height;
    x4->params.i_csp = X264_CSP_I420;
    x4->params.vui.i_sar_width = s->video.height * s->video.aspect.num;
    x4->params.vui.i_sar_height = s->video.width * s->video.aspect.den;
    x4->params.i_fps_num = s->video.frame_rate.num;
    x4->params.i_fps_den = s->video.frame_rate.den;

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
    free(x4->params.rc.psz_stat_out);
    free(x4->params.rc.psz_stat_in);
}

extern int
x4_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,
       tcvp_timer_t *t, muxed_stream_t *ms)
{
    x4_enc_t *x4;
    char *statfile;
    struct stat st;

    x4 = tcallocdz(sizeof(*x4), NULL, x4_free);
    x264_param_default(&x4->params);
    x4->params.pf_log = x4_log;
    x4->params.analyse.b_psnr = 0;

    tcconf_getvalue(cs, "cabac", "%i", &x4->params.b_cabac);
    tcconf_getvalue(cs, "qp", "%i", &x4->params.rc.i_qp_constant);
    tcconf_getvalue(cs, "gop_size", "%i", &x4->params.i_keyint_max);
    tcconf_getvalue(cs, "rc_buffer_size", "%i",
                    &x4->params.rc.i_vbv_buffer_size);
    tcconf_getvalue(cs, "bitrate", "%i", &x4->params.rc.i_bitrate);
    tcconf_getvalue(cs, "qpmin", "%i", &x4->params.rc.i_qp_min);
    tcconf_getvalue(cs, "qpmax", "%i", &x4->params.rc.i_qp_max);
    tcconf_getvalue(cs, "qpstep", "%i", &x4->params.rc.i_qp_step);
    tcconf_getvalue(cs, "cbr", "%i", &x4->params.rc.b_cbr);

    tcconf_getvalue(cs, "inter", "%i", &x4->params.analyse.inter);
    tcconf_getvalue(cs, "intra", "%i", &x4->params.analyse.intra);

    x4->params.rc.psz_stat_out = NULL;
    x4->params.rc.psz_stat_in = NULL;

    if(tcconf_getvalue(cs, "stats", "%s", &statfile) > 0){
        if(stat(statfile, &st)){
            x4->params.rc.b_stat_write = 1;
            x4->params.rc.psz_stat_out = statfile;
        } else {
            x4->params.rc.b_stat_read = 1;
            x4->params.rc.psz_stat_in = statfile;
        }
    }

    p->format = *s;
    p->format.common.codec = "video/h264";
    p->private = x4;

    return 0;
}

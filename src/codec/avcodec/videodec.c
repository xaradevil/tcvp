/**
    Copyright (C) 2003-2006  Michael Ahlberg, Måns Rullgård

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
#include <libavcodec/avcodec.h>
#include <avcodec_tc2.h>
#include "avc.h"

static int
do_decvideo(tcvp_pipe_t *p, tcvp_data_packet_t *pk, int probe)
{
    tcvp_data_packet_t *out;
    avc_codec_t *vc = p->private;
    uint8_t *inbuf;
    uint64_t dts = -1LL;
    int insize;

    if(pk->data){
        inbuf = pk->data[0];
        insize = pk->sizes[0];
    } else {
        p->next->input(p->next, (tcvp_packet_t *) pk);
        return 0;
    }

    if(pk->flags & TCVP_PKT_FLAG_PTS && !probe && vc->ctx->coded_frame){
        int cpn =
            vc->ctx->coded_frame->coded_picture_number + 1 + !vc->complete;
        cpn &= PTSQSIZE - 1;
        vc->ptsq[cpn] = pk->pts;
        tc2_print("AVCODEC", TC2_PRINT_DEBUG+1, "set pts %lli @%i\n",
                  pk->pts, cpn);
    } else if(pk->flags & TCVP_PKT_FLAG_DTS){
        tc2_print("AVCODEC", TC2_PRINT_DEBUG+1, "dts %lli\n", pk->dts);
        dts = pk->dts;
    }

    while(insize > 0){
        uint8_t *buf = NULL;
        int bufsize = 0;
        int s;

        if(vc->pctx){
            s = av_parser_parse(vc->pctx, vc->ctx, &buf, &bufsize,
                                inbuf, insize, 0, 0);
            if(s < 0)
                return probe? s: 0;
            inbuf += s;
            insize -= s;

            vc->complete = 0;
        } else {
            buf = inbuf;
            bufsize = insize;
        }

        if(bufsize){
            s = avcodec_decode_video(vc->ctx, vc->frame, &vc->complete,
                                     buf, bufsize);
            if(s < 0)
                return probe? s: 0;
            if(!vc->pctx){
                inbuf += s;
                insize -= s;
            }
        }

        if(vc->complete){
            int cpn = vc->frame->coded_picture_number & (PTSQSIZE - 1);
            int i;

            if(probe){
                vc->have_params = 1;
                break;
            }

            tc2_print("AVCODEC", TC2_PRINT_DEBUG+1, "coded %i\n",
                      vc->frame->coded_picture_number);

            out = tcallocdz(sizeof(*out), NULL, avc_free_packet);
            out->type = TCVP_PKT_TYPE_DATA;
            out->stream = pk->stream;
            out->data = vc->frame->data;
            out->sizes = malloc(4 * sizeof(*out->sizes));
            for(i = 0; i < 4; i++){
                out->sizes[i] = vc->frame->linesize[i];
                if(out->sizes[i] == 0)
                    break;
            }
            out->planes = i;

            out->flags = TCVP_PKT_FLAG_PTS;
            if(vc->ptsq[cpn] != -1LL){
                out->pts = vc->ptsq[cpn];
                vc->ptsq[cpn] = -1LL;
                vc->pts = out->pts * vc->ptsd;
                tc2_print("AVCODEC", TC2_PRINT_DEBUG+1, "get pts %lli @%i\n",
                          out->pts, cpn);
            } else if(dts != -1LL){
                tc2_print("AVCODEC", TC2_PRINT_DEBUG+1, "pts %lli\n", dts);
                out->pts = dts;
                vc->pts = dts * vc->ptsd;
                dts = -1LL;
            } else {
                out->pts = vc->pts / vc->ptsd;
            }
            vc->pts += vc->ptsn + vc->frame->repeat_pict * vc->ptsn / 2;

            out->private = NULL;
            p->next->input(p->next, (tcvp_packet_t *) out);
        }
    }

    tcfree(pk);

    return 0;
}

extern int
avc_decvideo(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    return do_decvideo(p, pk, 0);
}

static char *pixel_fmts[] = {
    [PIX_FMT_YUV420P] = "video/raw-i420",
    [PIX_FMT_YUYV422] = "video/raw-yuy2",
    [PIX_FMT_RGB24] = 0,
    [PIX_FMT_BGR24] = 0,
    [PIX_FMT_YUV422P] = "video/raw-yuv422p",
    [PIX_FMT_YUV444P] = 0,
    [PIX_FMT_RGB32] = 0,
    [PIX_FMT_YUV410P] = "video/raw-yvu9",
    [PIX_FMT_YUV411P] = 0,
    [PIX_FMT_RGB565] = "video/raw-rgb565",
    [PIX_FMT_RGB555] = "video/raw-rgb555",
    [PIX_FMT_GRAY8] = "video/raw-gray8",
    [PIX_FMT_MONOWHITE] = 0,
    [PIX_FMT_MONOBLACK] = 0,
    [PIX_FMT_PAL8] = 0,
    [PIX_FMT_YUVJ420P] = "video/raw-i420",
    [PIX_FMT_YUVJ422P] = "video/raw-yuv422p",
    [PIX_FMT_NB] = 0,
};

extern int
avc_probe_video(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    avc_codec_t *vc = p->private;
    int ret;

    if(!pk){
        p->format = *s;
        p->format.video.codec = "video/raw-i420";
        ret = PROBE_OK;
        goto out;
    }

    if(do_decvideo(p, pk, 1) < 0){
        ret = PROBE_DISCARD;
        goto out;
    }

    if(vc->have_params){
        if(!vc->ctx->width){
            ret = PROBE_AGAIN;
            goto out;
        }

        if(!pixel_fmts[vc->ctx->pix_fmt]){
            tc2_print("AVCODEC", TC2_PRINT_ERROR, "unknown pixel format %i\n",
                      vc->ctx->pix_fmt);
            ret = PROBE_FAIL;
            goto out;
        }

        p->format = *s;
        p->format.video.codec = pixel_fmts[vc->ctx->pix_fmt];
        p->format.video.width = vc->ctx->width;
        p->format.video.height = vc->ctx->height;

        tc2_print("AVCODEC", TC2_PRINT_DEBUG, "time_base = %i/%i\n",
                  vc->ctx->time_base.num, vc->ctx->time_base.den);

        if(vc->ctx->time_base.num && !p->format.video.frame_rate.num){
            p->format.video.frame_rate.num = vc->ctx->time_base.den;
            p->format.video.frame_rate.den = vc->ctx->time_base.num;
        }
        if(!p->format.video.frame_rate.num){
            p->format.video.frame_rate.num = 25;
            p->format.video.frame_rate.den = 1;
        }

        vc->ptsn = (uint64_t) 27000000 * p->format.video.frame_rate.den;
        vc->ptsd = p->format.video.frame_rate.num;

        tcreduce(&p->format.video.frame_rate);
        tc2_print("AVCODEC", TC2_PRINT_DEBUG, "frame rate %i/%i\n",
                  p->format.video.frame_rate.num,
                  p->format.video.frame_rate.den);
        if(vc->ctx->sample_aspect_ratio.num){
            p->format.video.aspect.num =
                vc->ctx->width * vc->ctx->sample_aspect_ratio.num;
            p->format.video.aspect.den =
                vc->ctx->height * vc->ctx->sample_aspect_ratio.den;
            tcreduce(&p->format.video.aspect);
        }
        ret = PROBE_OK;
        avcodec_flush_buffers(vc->ctx);
    } else {
        ret = PROBE_AGAIN;
    }

out:
    return ret;
}

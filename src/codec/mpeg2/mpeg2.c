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
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <mpeg2dec/mpeg2.h>
#include <mpeg2_tc2.h>

typedef struct mpeg_buf {
    uint8_t *data[3];
    struct mpeg_buf *prev, *next;
    int refs;
} mpeg_buf_t;

typedef struct mpeg_dec {
    mpeg2dec_t *mpeg2;
    const mpeg2_info_t *info;
    uint64_t pts;
    int flush;
    mpeg_buf_t *free_bufs, *used_bufs;
    int nfree;
    pthread_mutex_t lock;
} mpeg_dec_t;

typedef struct mpeg_packet {
    tcvp_data_packet_t pk;
    mpeg_buf_t *buf;
    int sizes[3];
    mpeg_dec_t *mpd;
} mpeg_packet_t;

#define MPEG_PTS_FLUSH (1<<31)

static void
mpeg_release_buf(mpeg_dec_t *mpd, mpeg_buf_t *mb)
{
    pthread_mutex_lock(&mpd->lock);

    if(--mb->refs)
        goto out;

    if(mb->prev)
        mb->prev->next = mb->next;
    else
        mpd->used_bufs = mb->next;

    if(mb->next)
        mb->next->prev = mb->prev;

    if(mpd->nfree < tcvp_codec_mpeg2_conf_bufpool){
        mb->prev = NULL;
        mb->next = mpd->free_bufs;
        mpd->free_bufs = mb;
        mpd->nfree++;
    } else {
        tc2_print("MPEG2", TC2_PRINT_DEBUG+2, "%p release\n", mb);
        tcfree(mb);
    }

out:
    pthread_mutex_unlock(&mpd->lock);
}

static void
mpeg_free_buf(void *p)
{
    mpeg_buf_t *b = p;
    tc2_print("MPEG2", TC2_PRINT_DEBUG+2, "%p free\n", b);
    free(b->data[0]);
    free(b->data[1]);
    free(b->data[2]);
}

static mpeg_buf_t *
mpeg_alloc(mpeg_dec_t *mpd)
{
    const mpeg2_sequence_t *seq = mpd->info->sequence;
    mpeg_buf_t *mb;

    pthread_mutex_lock(&mpd->lock);

    if(mpd->free_bufs){
        mb = mpd->free_bufs;
        mpd->free_bufs = mb->next;
        mpd->nfree--;
    } else {
        mb = tcallocd(sizeof(*mb), NULL, mpeg_free_buf);
        tc2_print("MPEG2", TC2_PRINT_DEBUG+2, "%p alloc\n", mb);
        mb->data[0] = malloc(seq->width * seq->height);
        mb->data[1] = malloc(seq->chroma_width * seq->chroma_height);
        mb->data[2] = malloc(seq->chroma_width * seq->chroma_height);
    }

    mb->refs = 1;
    mb->next = mpd->used_bufs;
    mb->prev = NULL;
    if(mpd->used_bufs)
        mpd->used_bufs->prev = mb;
    mpd->used_bufs = mb;

    pthread_mutex_unlock(&mpd->lock);

    return mb;
}

static void
mpeg_init_bufs(mpeg_dec_t *mpd)
{
    int i;

    mpeg2_custom_fbuf(mpd->mpeg2, 1);
    for(i = 0; i < 2; i++){
        mpeg_buf_t *fbuf = mpeg_alloc(mpd);
        mpeg2_set_buf(mpd->mpeg2, fbuf->data, fbuf);
    }
}

static void
mpeg_free_buflist(mpeg_buf_t *mb)
{
    while(mb){
        mpeg_buf_t *nmb = mb->next;
        tcfree(mb);
        mb = nmb;
    }
}

static void
mpeg_flush_bufs(mpeg_dec_t *mpd)
{
    pthread_mutex_lock(&mpd->lock);

    tc2_print("MPEG2", TC2_PRINT_DEBUG+1, "mpeg_flush_bufs free\n");
    mpeg_free_buflist(mpd->free_bufs);
    mpd->free_bufs = NULL;
    mpd->nfree = 0;

    tc2_print("MPEG2", TC2_PRINT_DEBUG+1, "mpeg_flush_bufs used\n");
    mpeg_free_buflist(mpd->used_bufs);
    mpd->used_bufs = NULL;

    pthread_mutex_unlock(&mpd->lock);
}

static void
mpeg_free_packet(void *p)
{
    mpeg_packet_t *mp = p;
    mpeg_release_buf(mp->mpd, mp->buf);
}

extern int
mpeg_decode(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    mpeg_dec_t *mpd = p->private;
    mpeg_buf_t *fbuf;
    int state;

    if(!pk->data){
        p->next->input(p->next, (tcvp_packet_t *) pk);
        return 0;
    }

    mpeg2_buffer(mpd->mpeg2, pk->data[0], pk->data[0] + pk->sizes[0]);
    if(!mpd->info)
        mpd->info = mpeg2_info(mpd->mpeg2);

    if((pk->flags & TCVP_PKT_FLAG_PTS)){
        uint32_t ptsl, ptsh;
        ptsl = pk->pts;
        ptsh = pk->pts >> 32;
        if(mpd->flush > 1){
            ptsh |= MPEG_PTS_FLUSH;
            mpd->flush = 1;
        }
        mpeg2_tag_picture(mpd->mpeg2, ptsl, ptsh);
        tc2_print("MPEG2", TC2_PRINT_DEBUG+1, "input pts %llu\n",
                  pk->pts / 300);
    }

    do {
        state = mpeg2_parse(mpd->mpeg2);
        switch(state){
        case STATE_SEQUENCE:
            mpeg_init_bufs(mpd);
            break;
        case STATE_PICTURE:
            fbuf = mpeg_alloc(mpd);
            mpeg2_set_buf(mpd->mpeg2, fbuf->data, fbuf);
            break;
        case STATE_SLICE:
        case STATE_END:
            if(mpd->info->display_fbuf){
                mpeg_packet_t *pic = tcallocdz(sizeof(*pic), NULL,
                                               mpeg_free_packet);

                pic->mpd = mpd;
                pic->buf = mpd->info->display_fbuf->id;
                pic->buf->refs++;
                pic->pk.stream = pk->stream;
                pic->pk.data = pic->buf->data;
                pic->pk.planes = 3;
                pic->pk.sizes = pic->sizes;
                pic->sizes[0] = mpd->info->sequence->picture_width;
                pic->sizes[1] = pic->sizes[0]/2;
                pic->sizes[2] = pic->sizes[0]/2;

                if(mpd->info->display_picture->flags &
                   PIC_FLAG_TOP_FIELD_FIRST){
                    pic->pk.flags |= TCVP_PKT_FLAG_TOPFIELDFIRST;
                }

                if(mpd->info->display_picture->flags & PIC_FLAG_TAGS){
                    uint32_t ptsh, ptsl;

                    ptsl = mpd->info->display_picture->tag;
                    ptsh = mpd->info->display_picture->tag2;
                    if(mpd->flush && (ptsh & MPEG_PTS_FLUSH)){
                        ptsh &= ~MPEG_PTS_FLUSH;
                        mpd->flush = 0;
                    }
                    if(!mpd->flush)
                        mpd->pts = ptsl | (uint64_t) ptsh << 32;
                }

                if(mpd->pts != -1LL){
                    int nf = mpd->info->display_picture->nb_fields;

                    if(nf < 2 && tcvp_codec_mpeg2_conf_force_frame_pic)
                        nf = 2;

                    tc2_print("MPEG2", TC2_PRINT_DEBUG+1, "pts %llu\n",
                              mpd->pts / 27);
                    pic->pk.flags |= TCVP_PKT_FLAG_PTS;
                    pic->pk.pts = mpd->pts;
                    mpd->pts += nf * mpd->info->sequence->frame_period / 2;
                }
                pic->pk.private = pic;
                p->next->input(p->next, (tcvp_packet_t *) pic);
            }

            if(mpd->info->discard_fbuf){
                mpeg_release_buf(mpd, mpd->info->discard_fbuf->id);
            }
            break;
        }
    } while(state != STATE_BUFFER);

    tcfree(pk);
    return 0;
}

extern int
mpeg_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    mpeg_dec_t *mpd = p->private;
    int state, ret = PROBE_AGAIN;
    const mpeg2_sequence_t *seq;
    mpeg_buf_t *fbuf;

    mpeg2_buffer(mpd->mpeg2, pk->data[0], pk->data[0] + pk->sizes[0]);
    mpd->info = mpeg2_info(mpd->mpeg2);

    do {
        state = mpeg2_parse(mpd->mpeg2);
        switch(state){
        case STATE_SEQUENCE:
            seq = mpd->info->sequence;
            p->format = *s;
            p->format.video.codec = "video/raw-i420";
            p->format.video.width = seq->picture_width;
            p->format.video.height = seq->picture_height;
            p->format.video.aspect.num = seq->pixel_width * seq->display_width;
            p->format.video.aspect.den = seq->pixel_height*seq->display_height;
            tcreduce(&p->format.video.aspect);
            p->format.video.frame_rate.num = 27000000;
            p->format.video.frame_rate.den = seq->frame_period;
            tcreduce(&p->format.video.frame_rate);
            if(!(seq->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE))
                p->format.video.flags |= TCVP_STREAM_FLAG_INTERLACED;
            mpeg_init_bufs(mpd);

            tc2_print("MPEG2", TC2_PRINT_DEBUG, "vbv_buffer_size %i\n",
                      seq->vbv_buffer_size * 8);
            tc2_print("MPEG2", TC2_PRINT_DEBUG, "bit_rate %i\n",
                      seq->byte_rate * 8);
            tc2_print("MPEG2", TC2_PRINT_DEBUG, "frame period %i\n",
                      seq->frame_period / 27);
            tc2_print("MPEG2", TC2_PRINT_DEBUG, "flags %x\n", seq->flags);

            ret = PROBE_OK;
            break;
        case STATE_PICTURE:
            fbuf = mpeg_alloc(mpd);
            mpeg2_set_buf(mpd->mpeg2, fbuf->data, fbuf);
            break;
        case STATE_INVALID:
            ret = PROBE_AGAIN;
        }
    } while(state != STATE_BUFFER && ret != PROBE_FAIL);

    tcfree(pk);
    return ret;
}

extern int
mpeg_flush(tcvp_pipe_t *p, int drop)
{
    mpeg_dec_t *mpd = p->private;

    if(drop){
        /* mpeg2_reset() apparently breaks seeking in some streams */
/*      mpeg2_reset(mpd->mpeg2, 1); */
        mpd->flush = 2;
        mpd->pts = -1LL;
        /* FIXME: the buffers should be flushed here, but the reference
           counting becomes a nightmare */
/*      mpeg_flush_bufs(mpd); */
    }

    return 0;
}

static void
mpeg_free(void *p)
{
    mpeg_dec_t *mpd = p;

    mpeg2_close(mpd->mpeg2);
    mpeg_flush_bufs(mpd);
}

extern int
mpeg_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
         muxed_stream_t *ms)
{
    mpeg_dec_t *mpd;

    mpd = tcallocdz(sizeof(*mpd), NULL, mpeg_free);
    mpd->mpeg2 = mpeg2_init();
    mpd->pts = -1LL;
    pthread_mutex_init(&mpd->lock, NULL);

    p->format.common.codec = "video/raw-i420";
    p->private = mpd;

    return 0;
}

extern int
mpeg_init(char *p)
{
    mpeg2_accel(tcvp_codec_mpeg2_conf_accel);
    return 0;
}

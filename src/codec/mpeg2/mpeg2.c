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

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <mpeg2dec/mpeg2.h>
#include <mpeg2_tc2.h>

typedef struct mpeg_dec {
    mpeg2dec_t *mpeg2;
    const mpeg2_info_t *info;
    uint64_t pts, npts;
    int ptsc;
    int pc;
    int pts_delay;
} mpeg_dec_t;

typedef struct mpeg_packet {
    packet_t pk;
    int sizes[3];
} mpeg_packet_t;

static void
mpeg_free_pk(packet_t *p)
{
    free(p);
}

static int
mpeg_decode(tcvp_pipe_t *p, packet_t *pk)
{
    mpeg_dec_t *mpd = p->private;
    int state;

    if(!pk->data){
	p->next->input(p->next, pk);
	return 0;
    }

    mpeg2_buffer(mpd->mpeg2, pk->data[0], pk->data[0] + pk->sizes[0]);
    if(!mpd->info)
	mpd->info = mpeg2_info(mpd->mpeg2);

    if((pk->flags & TCVP_PKT_FLAG_PTS) && !mpd->ptsc){
	mpd->npts = pk->pts;
	mpd->ptsc = mpd->info->sequence->flags & SEQ_FLAG_LOW_DELAY? 1:
	    mpd->pts_delay;
    }

    do {
	state = mpeg2_parse(mpd->mpeg2);
	if(state == STATE_SLICE || state == STATE_END){
	    if(mpd->info->display_fbuf){
		mpeg_packet_t *pic = malloc(sizeof(*pic));
		pic->pk.stream = pk->stream;
		pic->pk.data = mpd->info->display_fbuf->buf;
		pic->pk.planes = 3;
		pic->pk.sizes = pic->sizes;
		pic->sizes[0] = mpd->info->sequence->picture_width;
		pic->sizes[1] = pic->sizes[0]/2;
		pic->sizes[2] = pic->sizes[0]/2;

		if(mpd->ptsc > 0 && --mpd->ptsc == 0)
		    mpd->pts = mpd->npts;

		pic->pk.flags = TCVP_PKT_FLAG_PTS;
		pic->pk.pts = mpd->pts;
		mpd->pts += mpd->info->sequence->frame_period;
		pic->pk.free = mpeg_free_pk;
		pic->pk.private = pic;
		p->next->input(p->next, &pic->pk);
	    }
	    if(mpd->info->current_picture){
		if((mpd->info->current_picture->flags&PIC_MASK_CODING_TYPE) ==
		   PIC_FLAG_CODING_TYPE_B){
		    mpd->pc++;
		} else if(mpd->pc){
		    mpd->pts_delay = mpd->pc + 1;
		    mpd->pc = 0;
		}
	    }
	}
    } while(state != -1);

    pk->free(pk);
    return 0;
}

static int
mpeg_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    mpeg_dec_t *mpd = p->private;
    int state, ret = PROBE_AGAIN;
    const sequence_t *seq;

    mpeg2_buffer(mpd->mpeg2, pk->data[0], pk->data[0] + pk->sizes[0]);
    mpd->info = mpeg2_info(mpd->mpeg2);

    do {
	state = mpeg2_parse(mpd->mpeg2);
	switch(state){
	case STATE_SEQUENCE:
	    seq = mpd->info->sequence;
	    p->format = *s;
	    p->format.video.codec = "video/yuv-420";
	    p->format.video.width = seq->picture_width;
	    p->format.video.height = seq->picture_height;
	    p->format.video.aspect.num = seq->pixel_width * seq->display_width;
	    p->format.video.aspect.den = seq->pixel_height*seq->display_height;
	    p->format.video.frame_rate.num = 27000000;
	    p->format.video.frame_rate.den = seq->frame_period;
	    p->format.video.pixel_format = PIXEL_FORMAT_I420;
	    if(!(seq->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE))
		p->format.video.flags |= TCVP_STREAM_FLAG_INTERLACED;
	    ret = p->next->probe(p->next, NULL, &p->format);
	    break;
	case -1:
	    pk->free(pk);
	    break;
	case STATE_INVALID:
	    ret = PROBE_FAIL;
	}
    } while(state != -1 && ret != PROBE_FAIL);

    return ret;
}

static int
mpeg_flush(tcvp_pipe_t *p, int drop)
{
    return p->next->flush(p->next, drop);
}

static void
mpeg_free(void *p)
{
    tcvp_pipe_t *tp = p;
    mpeg_dec_t *mpd = tp->private;

    mpeg2_close(mpd->mpeg2);
    free(mpd);
}

extern tcvp_pipe_t *
mpeg_new(stream_t *s, conf_section *cs, timer__t **t)
{
    mpeg_dec_t *mpd;
    tcvp_pipe_t *p;

    mpd = calloc(1, sizeof(*mpd));
    mpd->mpeg2 = mpeg2_init();
    mpd->pts_delay = 1;

    p = tcallocdz(sizeof(*p), NULL, mpeg_free);
    p->input = mpeg_decode;
    p->probe = mpeg_probe;
    p->flush = mpeg_flush;
    p->private = mpd;

    return p;
}

extern int
mpeg_init(char *p)
{
    mpeg2_accel(tcvp_codec_mpeg2_conf_accel);
    return 0;
}

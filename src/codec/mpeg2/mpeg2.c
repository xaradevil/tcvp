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
#include <tcvp_types.h>
#include <mpeg2dec/mpeg2.h>
#include <mpeg2_tc2.h>

typedef struct mpeg_dec {
    mpeg2dec_t *mpeg2;
    const mpeg2_info_t *info;
    uint64_t pts;
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

    mpeg2_buffer(mpd->mpeg2, pk->data[0], pk->data[0] + pk->sizes[0]);
    if(!mpd->info)
	mpd->info = mpeg2_info(mpd->mpeg2);

    do {
	state = mpeg2_parse(mpd->mpeg2);
	if(state == STATE_SLICE || state == STATE_END){
	    if(mpd->info->display_fbuf){
		mpeg_packet_t *pic = malloc(sizeof(*pic));
		pic->pk.data = mpd->info->display_fbuf->buf;
		pic->pk.planes = 3;
		pic->pk.sizes = pic->sizes;
		pic->sizes[0] = mpd->info->sequence->picture_width;
		pic->sizes[1] = pic->sizes[0]/2;
		pic->sizes[2] = pic->sizes[0]/2;

		if(mpd->info->display_picture->flags & PIC_FLAG_PTS){
		    mpd->pts = mpd->info->display_picture->pts / 27;
		}
		pic->pk.pts = mpd->pts;
		mpd->pts += mpd->info->sequence->frame_period / 27;

		pic->pk.free = mpeg_free_pk;
		pic->pk.private = pic;
		p->next->input(p->next, &pic->pk);
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

    mpeg2_buffer(mpd->mpeg2, pk->data[0], pk->data[0] + pk->sizes[0]);
    mpd->info = mpeg2_info(mpd->mpeg2);

    do {
	state = mpeg2_parse(mpd->mpeg2);
	switch(state){
	case STATE_SEQUENCE:
	    s->video.width = mpd->info->sequence->picture_width;
	    s->video.height = mpd->info->sequence->picture_height;
	    s->video.frame_rate.num = 27000000;
	    s->video.frame_rate.den = mpd->info->sequence->frame_period;
	    s->video.pixel_format = PIXEL_FORMAT_I420;
	    ret = PROBE_OK;
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

static int
mpeg_free(tcvp_pipe_t *p)
{
    mpeg_dec_t *mpd = p->private;

    mpeg2_close(mpd->mpeg2);
    free(mpd);

    return 0;
}

extern tcvp_pipe_t *
mpeg_new(stream_t *s, int mode)
{
    mpeg_dec_t *mpd;
    tcvp_pipe_t *p;

    if(mode != CODEC_MODE_DECODE)
	return NULL;

    mpd = calloc(1, sizeof(*mpd));
    mpd->mpeg2 = mpeg2_init();

    p = calloc(1, sizeof(*p));
    p->input = mpeg_decode;
    p->free = mpeg_free;
    p->probe = mpeg_probe;
    p->flush = mpeg_flush;
    p->private = mpd;

    return p;
}

/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
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
    uint64_t pts;
    uint64_t rpts[16];
    int ptsc, flush;
} mpeg_dec_t;

typedef struct mpeg_packet {
    packet_t pk;
    u_char *data[3];
    int sizes[3];
} mpeg_packet_t;

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

    if((pk->flags & TCVP_PKT_FLAG_PTS)){
	mpd->rpts[mpd->ptsc] = pk->pts;
	mpeg2_pts(mpd->mpeg2, mpd->ptsc);
	if(++mpd->ptsc == 16)
	    mpd->ptsc = 0;
    }

    do {
	state = mpeg2_parse(mpd->mpeg2);
	if(state == STATE_SLICE || state == STATE_END){
	    if(mpd->info->display_picture){
		if(mpd->flush){
		    if((mpd->info->display_picture->flags &
			PIC_MASK_CODING_TYPE) != PIC_FLAG_CODING_TYPE_I)
			continue;
		    else
			mpd->flush = 0;
		}
	    }

	    if(mpd->info->display_fbuf){
		mpeg_packet_t *pic = tcalloc(sizeof(*pic));
		int i;

		pic->pk.stream = pk->stream;
		pic->pk.data = pic->data;
		pic->pk.planes = 3;
		pic->pk.sizes = pic->sizes;
		for(i = 0; i < 3; i++)
		    pic->data[i] = mpd->info->display_fbuf->buf[i];
		pic->sizes[0] = mpd->info->sequence->picture_width;
		pic->sizes[1] = pic->sizes[0]/2;
		pic->sizes[2] = pic->sizes[0]/2;

		if(mpd->info->display_picture->flags & PIC_FLAG_PTS){
		    mpd->pts = mpd->rpts[mpd->info->display_picture->pts];
		}

		pic->pk.flags = TCVP_PKT_FLAG_PTS;
		pic->pk.pts = mpd->pts;
		mpd->pts += mpd->info->sequence->frame_period;
		pic->pk.private = pic;
		p->next->input(p->next, &pic->pk);
	    }
	}
    } while(state != STATE_BUFFER);

    tcfree(pk);
    return 0;
}

static int
mpeg_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    mpeg_dec_t *mpd = p->private;
    int state, ret = PROBE_AGAIN;
    const mpeg2_sequence_t *seq;

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
	    ret = p->next->probe(p->next, NULL, &p->format);
	    break;
	case STATE_INVALID:
	    ret = PROBE_FAIL;
	}
    } while(state != STATE_BUFFER && ret != PROBE_FAIL);

    tcfree(pk);
    return ret;
}

static int
mpeg_flush(tcvp_pipe_t *p, int drop)
{
    mpeg_dec_t *mpd = p->private;

    if(drop){
	mpd->flush = 1;
	mpd->pts = 0;
	memset(mpd->rpts, 0, sizeof(mpd->rpts));
    }

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
mpeg_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	 muxed_stream_t *ms)
{
    mpeg_dec_t *mpd;
    tcvp_pipe_t *p;

    mpd = calloc(1, sizeof(*mpd));
    mpd->mpeg2 = mpeg2_init();

    p = tcallocdz(sizeof(*p), NULL, mpeg_free);
    p->format = *s;
    p->format.common.codec = "video/raw-i420";
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

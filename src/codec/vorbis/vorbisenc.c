/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcconf.h>
#include <tcvp_types.h>
#include <vorbis/vorbisenc.h>
#include <vorbis_tc2.h>

typedef struct vorbis_packet {
    packet_t pk;
    ogg_packet ogg, *op;
    int size;
} vorbis_packet_t;

typedef struct vorbis_enc {
    vorbis_info vi;
    vorbis_block vb;
    vorbis_dsp_state vd;
    vorbis_comment vc;
    float quality;
    int bps;
    int channels;
} vorbis_enc_t;

static void
int16tofloat(int16_t *in, float **out, int s, int c)
{
    int i, j;

    for(i = 0; i < s; i++){
	for(j = 0; j < c; j++){
	    out[j][i] = in[i * c + j] / 32768.0;
	}
    }
}

static void
ve_free_pk(void *p)
{
    vorbis_packet_t *vp = p;
    free(vp->ogg.packet);
}

static vorbis_packet_t *
ve_alloc(int s, ogg_packet *op)
{
    vorbis_packet_t *vp = tcallocdz(sizeof(*vp), NULL, ve_free_pk);
    vp->pk.stream = s;
    vp->pk.data = (u_char **) &vp->op;
    vp->pk.sizes = &vp->size;
    vp->pk.planes = 1;
    vp->op = &vp->ogg;
    vp->size = op->bytes;
    vp->ogg = *op;
    vp->ogg.packet = malloc(op->bytes);
    memcpy(vp->ogg.packet, op->packet, op->bytes);

    return vp;
}

static int
ve_input(tcvp_pipe_t *p, packet_t *pk)
{
    vorbis_enc_t *ve = p->private;
    ogg_packet op;
    float **buf;
    int samples;

    if(!pk->data){
	vorbis_analysis_wrote(&ve->vd, 0);
    } else {
	samples = pk->sizes[0] / ve->bps;
	buf = vorbis_analysis_buffer(&ve->vd, samples);
	int16tofloat((int16_t *)pk->data[0], buf, samples, ve->channels);
	vorbis_analysis_wrote(&ve->vd, samples);
    }

    while(vorbis_analysis_blockout(&ve->vd, &ve->vb) == 1){
	vorbis_analysis(&ve->vb, NULL);
	vorbis_bitrate_addblock(&ve->vb);

	while(vorbis_bitrate_flushpacket(&ve->vd, &op)){
	    vorbis_packet_t *vp = ve_alloc(pk->stream, &op);
	    p->next->input(p->next, &vp->pk);
	}
    }

    if(pk->data)
	tcfree(pk);
    else
	p->next->input(p->next, pk);

    return 0;
}

static int
ve_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    vorbis_enc_t *ve = p->private;
    ogg_packet header[3];	/* main, comments, codebook */
    vorbis_packet_t *op;
    int ret, i;

    if(vorbis_encode_setup_vbr(&ve->vi, s->audio.channels,
			       s->audio.sample_rate, ve->quality))
	return PROBE_FAIL;

    vorbis_encode_setup_init(&ve->vi);
    vorbis_analysis_init(&ve->vd, &ve->vi);
    vorbis_block_init(&ve->vd, &ve->vb);

    ve->bps = s->audio.channels * 2;
    ve->channels = s->audio.channels;

    p->format = *s;
    p->format.common.codec = "audio/vorbis";
    p->format.audio.bit_rate = ve->vi.bitrate_nominal;

    vorbis_analysis_headerout(&ve->vd, &ve->vc, &header[0], &header[1],
			      &header[2]);
    for(i = 0; i < 3; i++){
	op = ve_alloc(s->common.index, &header[i]);
	op->ogg.packetno = i;
	if((ret = p->next->probe(p->next, &op->pk, &p->format)) != PROBE_AGAIN)
	    break;
    }

    if(pk)
	tcfree(pk);
    return ret;
}

static int
ve_flush(tcvp_pipe_t *p, int drop)
{
    return p->next->flush(p->next, drop);
}

static void
ve_free(void *p)
{
    tcvp_pipe_t *tp = p;
    vorbis_enc_t *ve = tp->private;
    int i;

    vorbis_block_clear(&ve->vb);
    vorbis_dsp_clear(&ve->vd);
    vorbis_info_clear(&ve->vi);

    for(i = 0; i < ve->vc.comments; i++)
	free(ve->vc.user_comments[i]);
    free(ve->vc.user_comments);
    free(ve->vc.comment_lengths);
    free(ve);
}

static char *comments[] = {
    "title",
    "artist",
    "album"
};

extern tcvp_pipe_t *
ve_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t, muxed_stream_t *ms)
{
    tcvp_pipe_t *tp;
    vorbis_enc_t *ve;
    int nc, i, c;

    ve = calloc(1, sizeof(*ve));
    vorbis_info_init(&ve->vi);
    ve->quality = encoder_audio_vorbis_conf_quality;
    tcconf_getvalue(cs, "quality", "%f", &ve->quality);

    nc = sizeof(comments) / sizeof(comments[0]);
    ve->vc.user_comments = calloc(nc, sizeof(*ve->vc.user_comments));
    ve->vc.comment_lengths = calloc(nc, sizeof(*ve->vc.comment_lengths));
    for(i = 0, c = 0; i < nc; i++){
	char *cm = tcattr_get(ms, comments[i]);
	if(cm){
	    int cl = strlen(cm) + strlen(comments[i]) + 1;
	    char *vc = malloc(cl + 1);
	    sprintf(vc, "%s=%s", comments[i], cm);
	    ve->vc.user_comments[c] = vc;
	    ve->vc.comment_lengths[c] = cl;
	    c++;
	}
    }
    ve->vc.comments = c;
    ve->vc.vendor = "TCVP";

    tp = tcallocdz(sizeof(*tp), NULL, ve_free);
    tp->format = *s;
    tp->format.common.codec = "audio/vorbis";
    tp->input = ve_input;
    tp->probe = ve_probe;
    tp->flush = ve_flush;
    tp->private = ve;

    return tp;
}

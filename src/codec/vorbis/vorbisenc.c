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
#include <tcconf.h>
#include <tcvp_types.h>
#include <vorbis/vorbisenc.h>
#include <vorbis_tc2.h>

typedef struct vorbis_packet {
    tcvp_data_packet_t pk;
    u_char *data;
    int size[2];
} vorbis_packet_t;

typedef struct vorbis_enc {
    vorbis_info vi;
    vorbis_block vb;
    vorbis_dsp_state vd;
    vorbis_comment vc;
    float quality;
    int bps;
    int channels;
    uint64_t gpos;
    u_char *headers;
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
    free(vp->data);
}

static vorbis_packet_t *
ve_alloc(int s, ogg_packet *op, int samples)
{
    vorbis_packet_t *vp = tcallocdz(sizeof(*vp), NULL, ve_free_pk);
    vp->pk.stream = s;
    vp->pk.data = &vp->data;
    vp->pk.sizes = vp->size;
    vp->pk.planes = 1;
    vp->size[0] = op->bytes;
    vp->size[1] = samples;
    vp->data = malloc(op->bytes);
    memcpy(vp->data, op->packet, op->bytes);

    return vp;
}

extern int
ve_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
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
	    vorbis_packet_t *vp = ve_alloc(pk->stream, &op,
					   op.granulepos - ve->gpos);
	    p->next->input(p->next, (tcvp_packet_t *) vp);
	    ve->gpos = op.granulepos;
	}
    }

    if(pk->data)
	tcfree(pk);
    else
	p->next->input(p->next, (tcvp_packet_t *) pk);

    return 0;
}

extern int
ve_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    vorbis_enc_t *ve = p->private;
    ogg_packet op[3];	/* main, comments, codebook */
    u_char *cdp;
    int cds, i;

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

    vorbis_analysis_headerout(&ve->vd, &ve->vc, &op[0], &op[1], &op[2]);

    cds = op[0].bytes + op[1].bytes + op[2].bytes + 6;
    cdp = malloc(cds);
    ve->headers = cdp;
    p->format.common.codec_data = cdp;
    p->format.common.codec_data_size = cds;

    for(i = 0; i < 3; i++){
	*cdp++ = op[i].bytes >> 8;
	*cdp++ = op[i].bytes & 0xff;
	memcpy(cdp, op[i].packet, op[i].bytes);
	cdp += op[i].bytes;
    }

    tcfree(pk);
    return PROBE_OK;
}

static void
ve_free(void *p)
{
    vorbis_enc_t *ve = p;
    int i;

    vorbis_block_clear(&ve->vb);
    vorbis_dsp_clear(&ve->vd);
    vorbis_info_clear(&ve->vi);

    for(i = 0; i < ve->vc.comments; i++)
	free(ve->vc.user_comments[i]);
    free(ve->vc.user_comments);
    free(ve->vc.comment_lengths);
    free(ve->headers);
}

static char *comments[] = {
    "title",
    "artist",
    "album"
};

extern int
ve_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
       muxed_stream_t *ms)
{
    vorbis_enc_t *ve;
    int nc, i, c;

    ve = tcallocdz(sizeof(*ve), NULL, ve_free);
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

    p->format.common.codec = "audio/vorbis";
    p->private = ve;

    return 0;
}

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
#include <vorbis/vorbisfile.h>
#include <vorbis_tc2.h>

typedef struct {
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;
    char *buf;
} VorbisContext_t;

static void
vorbis_free_packet(void *v)
{
    packet_t *p = v;
    free(p->sizes);
}


static inline int
conv(int samples, float **pcm, char *buf, int channels) {
    int i, j, val ;
    ogg_int16_t *ptr, *data = (ogg_int16_t*)buf ;
    float *mono ;
 
    for(i = 0; i < channels; i++){
	ptr = &data[i];
	mono = pcm[i] ;
	
	for(j = 0; j < samples; j++) {
	    
	    val = mono[j] * 32767.f;
	    
	    if(val > 32767) val = 32767;
	    if(val < -32768) val = -32768;
	   	    
	    *ptr = val;
	    ptr += channels;
	}
    }
    
    return 0;
}


extern int
vorbis_decode(tcvp_pipe_t *p, packet_t *pk)
{
    packet_t *out;
    VorbisContext_t *vc = p->private;
    int samples, total_samples, total_bytes;
    float **pcm ;
    ogg_packet *op;

    if(!pk->data){
	p->next->input(p->next, pk);
	return 0;
    }

    op = (ogg_packet *) pk->data[0];

    if(vorbis_synthesis(&vc->vb, op) == 0)
	vorbis_synthesis_blockin(&vc->vd, &vc->vb);

    total_samples = 0;
    total_bytes = 0;

    while((samples = vorbis_synthesis_pcmout(&vc->vd, &pcm)) > 0) {
	conv(samples, pcm, (char*)vc->buf + total_bytes, vc->vi.channels);
	total_bytes += samples * 2 * vc->vi.channels;
	total_samples += samples;
	vorbis_synthesis_read(&vc->vd, samples);
    }

    out = tcallocd(sizeof(*out), NULL, vorbis_free_packet);
    out->stream = pk->stream;
    out->data = (u_char **) &out->private;
    out->sizes = malloc(sizeof(*out->sizes));
    out->sizes[0] = total_bytes;
    out->planes = 1;
    out->flags = 0;
    if(pk->flags & TCVP_PKT_FLAG_PTS){
	out->flags |= TCVP_PKT_FLAG_PTS;
	out->pts = pk->pts;
    }
    out->private = vc->buf;

    p->next->input(p->next, out);

    tcfree(pk);

    return 0;
}

extern int
vorbis_read_header(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    ogg_packet *op=(ogg_packet*)pk->data[0];
    VorbisContext_t *vc = p->private;
    int ret = PROBE_FAIL;

    if(op->packetno < 3) {
	vorbis_synthesis_headerin(&vc->vi, &vc->vc, op);
	if(op->packetno < 2) {	
	    ret = PROBE_AGAIN;
	} else {
	    p->format = *s;
	    p->format.audio.codec = "audio/pcm-s16" TCVP_ENDIAN;
	    p->format.audio.sample_rate = vc->vi.rate;
	    p->format.audio.channels = vc->vi.channels;
	    p->format.audio.bit_rate = vc->vi.rate * vc->vi.channels * 16;
	    s->audio.sample_rate = vc->vi.rate;
	    s->audio.channels = vc->vi.channels;
	    s->audio.bit_rate = vc->vi.bitrate_nominal;
	    vorbis_synthesis_init(&vc->vd, &vc->vi);
	    vorbis_block_init(&vc->vd, &vc->vb); 
	    ret = PROBE_OK;
	}
    }
    tcfree(pk);
    return ret;
}

static void
vorbis_free_pipe(void *p)
{
    VorbisContext_t *vc = p;

    vorbis_block_clear(&vc->vb);
    vorbis_dsp_clear(&vc->vd);
    vorbis_comment_clear(&vc->vc);
    vorbis_info_clear(&vc->vi);

    if(vc->buf)
	free(vc->buf);
}

extern int
vorbis_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	   muxed_stream_t *ms)
{
    VorbisContext_t *vc;

    vc = tcallocdz(sizeof(*vc), NULL, vorbis_free_pipe);

    vorbis_info_init(&vc->vi);
    vorbis_comment_init(&vc->vc);

    vc->buf = malloc(131072);

    p->format.audio.codec = "audio/pcm-s16" TCVP_ENDIAN;
    p->private = vc;

    return 0;
}

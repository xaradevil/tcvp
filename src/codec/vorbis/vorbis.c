/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

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
#include <vorbis/vorbisfile.h>
#include <vorbis_tc2.h>

typedef struct {
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;
    tcvp_pipe_t *out;
    char *buf;
} VorbisContext_t;

static void
vorbis_free_packet(packet_t *p)
{
    free(p->sizes);
    free(p);
}


static inline int conv(int samples, float **pcm, char *buf, int channels) {
    int i, j, val ;
    ogg_int16_t *ptr, *data = (ogg_int16_t*)buf ;
    float *mono ;
 
    for(i = 0 ; i < channels ; i++){
	ptr = &data[i];
	mono = pcm[i] ;
	
	for(j = 0 ; j < samples ; j++) {
	    
	    val = mono[j] * 32767.f;
	    
	    if(val > 32767) val = 32767 ;
	    if(val < -32768) val = -32768 ;
	   	    
	    *ptr = val ;
	    ptr += channels;
	}
    }
    
    return 0 ;
}


static int
vorbis_decode(tcvp_pipe_t *p, packet_t *pk)
{
    packet_t *out;
    VorbisContext_t *vc = p->private;
    int samples, total_samples, total_bytes;
    float **pcm ;
    ogg_packet *op=(ogg_packet*)pk->data[0];

    if(op->packetno < 3) {
	vorbis_synthesis_headerin(&vc->vi, &vc->vc, op);
    } else {
	if(op->packetno == 3) {
	    fprintf(stderr, "Channels: %d Rate:%dHz\n", vc->vi.channels,
		    vc->vi.rate);
	    vorbis_synthesis_init(&vc->vd, &vc->vi) ;
	    vorbis_block_init(&vc->vd, &vc->vb); 
	}

	if(vorbis_synthesis(&vc->vb, op) == 0)
	    vorbis_synthesis_blockin(&vc->vd, &vc->vb) ;
    
	total_samples = 0;
	total_bytes = 0;

	while((samples = vorbis_synthesis_pcmout(&vc->vd, &pcm)) > 0) {
	    conv(samples, pcm, (char*)vc->buf + total_bytes, vc->vi.channels);
	    total_bytes += samples * 2 * vc->vi.channels;
	    total_samples += samples;
	    vorbis_synthesis_read(&vc->vd, samples);
	}

	out = malloc(sizeof(*out));
	out->data = (u_char **) &out->private;
	out->sizes = malloc(sizeof(size_t));
	out->sizes[0] = total_bytes;
	out->planes = 1;
	out->pts = 0;
	out->free = vorbis_free_packet;
	out->private = vc->buf;
	vc->out->input(vc->out, out);
    }
    pk->free(pk);

    return 0;
}


static int
vorbis_free_pipe(tcvp_pipe_t *p)
{
    VorbisContext_t *vc = p->private;

    vorbis_block_clear(&vc->vb);
    vorbis_dsp_clear(&vc->vd);
        vorbis_comment_clear(&vc->vc);
    vorbis_info_clear(&vc->vi);

    if(vc->buf)
	free(vc->buf);
    free(vc);
    free(p);

    return 0;
}


extern tcvp_pipe_t *
vorbis_new(stream_t *s, int mode, tcvp_pipe_t *out)
{
    tcvp_pipe_t *p = NULL;
    VorbisContext_t *vc;

    if(mode != CODEC_MODE_DECODE)
	return NULL;

    vc = malloc(sizeof(VorbisContext_t));

    vorbis_info_init(&vc->vi) ;
    vorbis_comment_init(&vc->vc) ;

    vc->out=out;

    vc->buf=malloc(131072);

    p = malloc(sizeof(*p));
    if(mode == CODEC_MODE_DECODE)
	p->input = vorbis_decode;
    p->start = NULL;
    p->stop = NULL;
    p->free = vorbis_free_pipe;
    p->private = vc;

    return p;
}
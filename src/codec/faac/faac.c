/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <string.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <faac.h>
#include <aac_enc_tc2.h>

typedef struct faac_enc {
    faacEncHandle fe;
    u_char *buf;
    int bufsize;
    int bpos;
    int encbufsize;
    int samples;
    int ssize;
    uint64_t pts;
} faac_enc_t;

typedef struct faac_packet {
    packet_t pk;
    u_char *data;
    int size;
} faac_packet_t;

#define min(a, b) ((a)<(b)?(a):(b))

static void
faac_free_pk(void *p)
{
    faac_packet_t *pk = p;
    free(pk->data);
}

static int
encode(tcvp_pipe_t *p, packet_t *pk)
{
    faac_enc_t *ae = p->private;
    int size = ae->bpos, esize;
    u_char *buf = malloc(ae->encbufsize);

    if(size < ae->bufsize)
	memset(ae->buf + ae->bpos, 0, ae->bufsize - size);
    esize = faacEncEncode(ae->fe, (int32_t *) ae->buf, ae->samples,
			  buf, ae->encbufsize);
    if(esize > 0){
	faac_packet_t *opk = tcallocdz(sizeof(*opk), NULL, faac_free_pk);
	opk->pk.stream = pk->stream;
	opk->pk.data = &opk->data;
	opk->pk.sizes = &opk->size;
	opk->pk.planes = 1;
	opk->data = buf;
	opk->size = esize;
	if(ae->pts != -1){
	    opk->pk.flags |= TCVP_PKT_FLAG_PTS;
	    opk->pk.pts = ae->pts;
	    ae->pts = -1;
	}
	p->next->input(p->next, &opk->pk);
    }

    ae->bpos = 0;

    return 0;
}

extern int
faac_input(tcvp_pipe_t *p, packet_t *pk)
{
    faac_enc_t *ae = p->private;
    u_char *data;
    int size;

    if(!pk->data){
	if(ae->bpos)
	    encode(p, pk);
	p->next->input(p->next, pk);
	return 0;
    }

    if(pk->flags & TCVP_PKT_FLAG_PTS)
	ae->pts = pk->pts -
	    ae->bpos * 27000000 / ae->ssize / p->format.audio.sample_rate;

    data = pk->data[0];
    size = pk->sizes[0];

    while(size > 0){
	int s = min(size, ae->bufsize - ae->bpos);
	memcpy(ae->buf + ae->bpos, data, s);
	ae->bpos += s;
	data += s;
	size -= s;
	if(ae->bpos == ae->bufsize){
	    encode(p, pk);
	}
    }

    tcfree(pk);
    return 0;
}

extern int
faac_flush(tcvp_pipe_t *p, int drop)
{ 
    faac_enc_t *ae = p->private;

    if(drop)
	ae->bpos = 0;

    return 0;
}

extern int
faac_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    faac_enc_t *ae = p->private;
    faacEncConfiguration *fec;
    u_long insamples, bufsize;

    ae->fe = faacEncOpen(s->audio.sample_rate, s->audio.channels,
			 &insamples, &bufsize);
    if(!ae->fe)
	return PROBE_FAIL;

    fec = faacEncGetCurrentConfiguration(ae->fe);
    if(!strcmp(s->common.codec, "audio/pcm-s16" TCVP_ENDIAN)){
	fec->inputFormat = FAAC_INPUT_16BIT;
	ae->ssize = 2;
    } else if(!strcmp(s->common.codec, "audio/pcm-s24" TCVP_ENDIAN)){
	fec->inputFormat = FAAC_INPUT_24BIT;
	ae->ssize = 3;
    } else if(!strcmp(s->common.codec, "audio/pcm-s32" TCVP_ENDIAN)){
	fec->inputFormat = FAAC_INPUT_32BIT;
	ae->ssize = 4;
    } else {
	fprintf(stderr, "FAAC: unsupported sample format %s\n",
		s->common.codec);
	return PROBE_FAIL;
    }
    fec->outputFormat = 1;
    fec->mpegVersion = MPEG2;
    fec->aacObjectType = LOW;
    fec->allowMidside = 1;
    if(!faacEncSetConfiguration(ae->fe, fec))
	return PROBE_FAIL;

    ae->bufsize = insamples * ae->ssize;
    ae->buf = malloc(ae->bufsize);
    ae->samples = insamples;
    ae->encbufsize = bufsize;

    p->format.common.codec = "audio/aac";

    return PROBE_OK;
}

static void
faac_free(void *p)
{
    faac_enc_t *ae = p;

    if(ae->fe)
	faacEncClose(ae->fe);
    if(ae->buf)
	free(ae->buf);
}

extern int
faac_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	 muxed_stream_t *ms)
{
    faac_enc_t *ae = tcallocdz(sizeof(*ae), NULL, faac_free);
    ae->pts = -1;
    p->format.common.codec = "audio/aac";
    p->private = ae;

    return 0;
}

/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

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
#include <string.h>
#include <tctypes.h>
#include <pcmfmt_tc2.h>
#include <pcmmod.h>

#define TAG(a,b,c,d) (a + (b << 8) + (c << 16) + (d << 24))

static char *aid2codec(int id, int bits);

static char *
tag2str(uint32_t tag, char *s)
{
    int i;
    for(i = 0; i < 5; i++){
	s[i] = tag & 0xff;
	tag >>= 8;
    }
    return s;
}

extern muxed_stream_t *
wav_open(char *name, url_t *u, tcconf_section_t *conf, tcvp_timer_t *tm)
{
    uint32_t tag, size;
    char *codec, *extra = NULL;
    uint16_t channels, bits, extrasize = 0, fmt, align;
    uint32_t srate, brate;
    int data_size = 0;
    int data = 0;
    char tags[5];

    url_getu32l(u, &tag);
    if(tag != TAG('R','I','F','F'))
	return NULL;
    url_getu32l(u, &size);
    url_getu32l(u, &tag);
    if(tag != TAG('W','A','V','E'))
	return NULL;

    while(!data && !url_getu32l(u, &tag)){
	url_getu32l(u, &size);
	switch(tag){
	case TAG('f','m','t',' '): {
	    url_getu16l(u, &fmt);
	    url_getu16l(u, &channels);
	    url_getu32l(u, &srate);
	    url_getu32l(u, &brate);
	    url_getu16l(u, &align);
	    url_getu16l(u, &bits);
	    if(size > 16){
		url_getu16l(u, &extrasize);
		if(extrasize){
		    extra = malloc(extrasize);
		    u->read(extra, 1, extrasize, u);
		}
	    }
	    break;
	}
	case TAG('d','a','t','a'):
	    data_size = size;
	    data = 1;
	    break;
	default:
	    fprintf(stderr, "WAV: unknown header %s\n", tag2str(tag, tags));
	    u->seek(u, size, SEEK_CUR);
	    break;
	}
	if(size & 1)
	    u->seek(u, 1, SEEK_CUR);
    }

    if(!data)
	return NULL;

    codec = aid2codec(fmt, bits);
    if(!codec)
	return NULL;
    brate *= 8;

    if(!strcmp(codec, "audio/mpeg"))
	return audio_mpeg_open(name, u, conf, tm);

    return pcm_open(u, codec, channels, srate, data_size / align, brate,
		    bits, extra, extrasize);
}

#define WAV_HEADER_SIZE 44

#define store(v,d,s) do {			\
    *(uint##s##_t *)d = v;			\
    d += s / 8;					\
} while(0)

extern u_char *
wav_header(stream_t *s, int *size)
{
    u_char *head, *p;
    int bits;

    if(s->stream_type != STREAM_TYPE_AUDIO)
	return NULL;

    if(!strcmp(s->audio.codec, "audio/pcm-u8")){
	bits = 8;
    } else if(!strcmp(s->audio.codec, "audio/pcm-s16le")){
	bits = 16;
    } else {
	return NULL;
    }

    p = head = malloc(WAV_HEADER_SIZE);

    store(TAG('R','I','F','F'), p, 32);
    store(0, p, 32);
    store(TAG('W','A','V','E'), p, 32);

    store(TAG('f','m','t',' '), p, 32);
    store(16, p, 32);		/* size */
    store(1, p, 16);		/* format */
    store(s->audio.channels, p, 16);
    store(s->audio.sample_rate, p, 32);
    store(s->audio.bit_rate / 8, p, 32);
    store(bits * s->audio.channels / 8, p, 16);	/* block align */
    store(bits, p, 16);

    store(TAG('d','a','t','a'), p, 32);
    store(0, p, 32);

    *size = p - head;
    return head;
}

static int
wav_close(pcm_write_t *pcm)
{
    uint64_t size = pcm->u->tell(pcm->u);
    uint32_t v = size - 8;

    pcm->u->seek(pcm->u, 4, SEEK_SET);
    url_putu32l(pcm->u, v);

    v = size - WAV_HEADER_SIZE;
    pcm->u->seek(pcm->u, WAV_HEADER_SIZE - 4, SEEK_SET);
    url_putu32l(pcm->u, v);

    return 0;
}

extern int
wav_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    pcm_write_t *pcm = p->private;
    u_char *head;
    int hsize;

    if(pcm->probed)
	return PROBE_FAIL;

    if(!(head = wav_header(s, &hsize)))
	return PROBE_FAIL;

    pcm->u->write(head, 1, hsize, pcm->u);

    pcm->probed = 1;
    pcm->close = wav_close;

    return PROBE_OK;
}

static struct {
    int id;
    char *codec;
} acodec_ids[] = {
    { 0x01, "audio/pcm-s16le" },
    { 0x02, "audio/adpcm-ms" },
    { 0x06, "audio/pcm-alaw" },
    { 0x07, "audio/pcm-ulaw" },
    { 0x11, "audio/adpcm-ima-wav" },
    { 0x16, "audio/adpcm-g723" },
    { 0x31, "audio/gsm" },
    { 0x40, "audio/adpcm-g721" },
    { 0x50, "audio/mpeg" },
    { 0x55, "audio/mpeg" },
    { 0x160, "audio/wmav1" },
    { 0x161, "audio/wmav2" },
    { 0x2000, "audio/ac3" },
    { 0, NULL },
};

static char *
aid2codec(int id, int bits)
{
    int i;

    if(id == 1)
	return bits == 8? "audio/pcm-u8": "audio/pcm-s16le";

    for(i = 0; acodec_ids[i].codec; i++)
	if(id == acodec_ids[i].id)
	    break;

    return acodec_ids[i].codec;
}

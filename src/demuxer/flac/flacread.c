/**
    Copyright (C) 2005-2006  Michael Ahlberg, Måns Rullgård

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
#include <tcalloc.h>
#include <ctype.h>
#include <flacfile_tc2.h>
#include "flac.h"

#define BUFSIZE 32768

typedef struct flacread {
    url_t *url;
    stream_t s;
    int used;
    u_char *buf;
    int bufsize;
    int bpos, bend;
    int frame;
    int eof;
} flacread_t;

typedef struct flacread_packet {
    tcvp_data_packet_t pk;
    u_char *data, *buf;
    int size;
} flacread_packet_t;

#define MIN_HEADER_SIZE 6

static void
flr_free_pk(void *p)
{
    flacread_packet_t *fp = p;
    tcfree(fp->buf);
}

static int
utf8_size(int v)
{
    int s = 1, m = 0x40;

    if(!(v & 0x80))
	return 1;

    while(v & m){
	s++;
	m >>= 1;
    }

    tc2_print("FLAC", TC2_PRINT_DEBUG+3, "utf8_size(%x) = %i\n",
	      v, s);

    return s;
}

static int
flr_frame_header(u_char *buf, int size)
{
    u_char *p = buf;
    int hsize;
    int bs, v;
    int fns;

    if(*p++ != 0xff)
	return -1;
    if(*p++ != 0xf8)
	return -1;

    size -= 2;

    bs = *p++;			/* block size, sample rate */
    size --;

    v = bs & 0xf;
    if(v > 0 && v < 4)
	return -1;
    if(v == 0xf)
	return -1;

    v = *p++;			/* channels, sample size */
    size --;

    if(v & 1)
	return -1;
    if((v & 0xf0) > 0xa0)
	return -1;
    if((v & 0xe) == 6 || (v & 0xe) == 0xe)
	return -1;

    fns = utf8_size(*p);
    p += fns;
    size -= fns;

    if((bs & 0xf0) == 0x60){	/* block size */
	p++;
	size--;
    } else if((bs & 0xf0) == 0x70){
	p += 2;
	size -= 2;
    }

    if((bs & 0xf) == 0xc){	/* sample rate */
	p++;
	size--;
    } else if((bs & 0xc) == 0xc){
	p += 2;
	size -= 2;
    }

    p++;			/* crc */
    size--;

    hsize = p - buf;

    if(size < hsize)
	return hsize;

    if(flac_crc8(buf, hsize))
	return -1;

    return hsize;
}

extern tcvp_packet_t *
flr_packet(muxed_stream_t *ms, int s)
{
    flacread_t *flr = ms->private;
    flacread_packet_t *fp = NULL;
    int i = flr->bpos + 4;
    int hsize = 0;

    while(!fp && flr->eof < 2){
	if(flr->bend < flr->bufsize && !flr->eof){
	    int s = flr->bufsize - flr->bend;
	    tc2_print("FLAC", TC2_PRINT_DEBUG+2, "read %i @%i\n",
		      s, flr->bend);
	    s = flr->url->read(flr->buf + flr->bend, 1, s, flr->url);
	    if(s > 0){
		flr->bend += s;
	    } else {
		flr->eof = 1;
	    }
	}

	tc2_print("FLAC", TC2_PRINT_DEBUG+2, "searching for header @%4x\n", i);

	for(; i < flr->bend - MIN_HEADER_SIZE; i++){
	    hsize = flr_frame_header(flr->buf + i, flr->bend - i);
	    if(hsize > 0)
		break;
	}

	tc2_print("FLAC", TC2_PRINT_DEBUG+2,
		  "hsize = %i, i = %i, end = %i\n", hsize, i, flr->bend);

	if((hsize > 0 && i <= flr->bend - hsize) || flr->eof){
	    uint16_t crc, fcrc;
	    int size;

	    if(i == flr->bend - MIN_HEADER_SIZE){
		i = flr->bend;
		flr->eof++;
	    }

	    size = i - flr->bpos;

	    fcrc = htob_16(unaligned16(flr->buf + i - 2));
	    crc = flac_crc16(flr->buf + flr->bpos, size - 2);
	    tc2_print("FLAC", TC2_PRINT_DEBUG+1,
		      "frame %3i @%4x, size %5i header %i, crc %04x %04x\n",
		      flr->frame++, flr->bpos, size, hsize, crc, fcrc);

	    if(crc == fcrc){
		fp = tcallocdz(sizeof(*fp), NULL, flr_free_pk);
		fp->pk.data = &fp->data;
		fp->pk.sizes = &fp->size;
		fp->size = size;
		fp->data = flr->buf + flr->bpos;
		fp->buf = tcref(flr->buf);
	    }

	    if(crc == fcrc || (i < flr->bend - 2 &&
			       flr->buf[flr->bpos+2] == flr->buf[i+2])){
		if(crc != fcrc)
		    tc2_print("FLAC", TC2_PRINT_WARNING, "bad frame CRC\n");
		flr->bpos = i;
		i += hsize;
	    } else {
		i++;
	    }
	}

	if(i >= flr->bend - (hsize > 0? hsize: MIN_HEADER_SIZE) && !flr->eof){
	    u_char *nb;

	    if(flr->bpos == 0)
		flr->bufsize *= 2;

	    nb = tcalloc(flr->bufsize);
	    memmove(nb, flr->buf + flr->bpos, flr->bend - flr->bpos);
	    tcfree(flr->buf);
	    flr->buf = nb;
	    flr->bend -= flr->bpos;
	    i -= flr->bpos;
	    flr->bpos = 0;
	}
    }

    return (tcvp_packet_t *) fp;
}

extern int
flr_streaminfo(muxed_stream_t *ms, stream_t *st, u_char *p, int size)
{
    uint64_t samples;
    uint32_t scb;

    if(size != 34)
	return -1;

    p += 10;

    scb = htob_32(unaligned32(p));
    p += 4;

    st->audio.sample_rate = scb >> 12;
    st->audio.channels = ((scb >> 9) & 7) + 1;

    samples = (uint64_t) (scb & 0xf) << 32;
    samples += htob_32(unaligned32(p));
    st->audio.samples = samples;

    ms->time = samples * 27000000LL / st->audio.sample_rate;

    return 0;
}

static int
flr_comment(muxed_stream_t *ms, char *p, int size)
{
    int s, n, j;

    if(size < 4)
	return -1;

    s = htol_32(unaligned32(p));
    p += 4;
    size -= 4;

    if(size < s + 4)
	return -1;

    p += s;
    size -= s;

    n = htol_32(unaligned32(p));
    p += 4;
    size -= 4;

    while(size >= 4){
	char *t, *v;
	int tl, vl;

	s = htol_32(unaligned32(p));
	p += 4;
	size -= 4;

	if(size < s)
	    break;

	t = p;
	p += s;
	size -= s;
	n--;

	v = memchr(t, '=', s);
	if(!v)
	    continue;

	tl = v - t;
	vl = s - tl - 1;
	v++;

	if(tl && vl){
	    char tt[tl + 1];
	    char *ct;

	    for(j = 0; j < tl; j++)
		tt[j] = tolower(t[j]);
	    tt[tl] = 0;

	    ct = malloc(vl + 1);
	    memcpy(ct, v, vl);
	    ct[vl] = 0;
	    tcattr_set(ms, tt, ct, NULL, free);
	}
    }

    if(size > 0)
	tc2_print("FLAC", TC2_PRINT_WARNING,
		  "%i bytes of comment header remain\n", size);
    if(n > 0)
	tc2_print("FLAC", TC2_PRINT_WARNING,
		  "truncated comment header, %i comments not found\n", n);

    return 0;
}

static int
flr_metadata(url_t *u, flac_metadata_t *fm)
{
    uint32_t mbh;

    if(url_getu32b(u, &mbh))
	return -1;

    fm->type = (mbh >> 24) & 0x7f;
    fm->last = mbh >> 31;
    fm->size = mbh & 0xffffff;

    fm->data = malloc(fm->size);
    if(!fm->data)
	return -1;

    if(u->read(fm->data, 1, fm->size, u) < fm->size){
	free(fm->data);
	return -1;
    }

    return 0;
}

static int
flr_header(muxed_stream_t *ms)
{
    flacread_t *flr = ms->private;
    flac_metadata_t fm;
    int err = 0;

    while(!err && !flr_metadata(flr->url, &fm)){
	switch(fm.type){
	case FLAC_META_STREAMINFO:
	    err = flr_streaminfo(ms, &flr->s, fm.data, fm.size);
	    flr->s.common.codec_data = fm.data;
	    flr->s.common.codec_data_size = fm.size;
	    fm.data = NULL;
	    break;
	case FLAC_META_VORBIS_COMMENT:
	    err = flr_comment(ms, fm.data, fm.size);
	    break;
	case FLAC_META_SEEKTABLE:
	    break;
	case FLAC_META_APPLICATION:
	    break;
	case FLAC_META_CUESHEET:
	    break;
	case FLAC_META_PADDING:
	    break;
	default:
	    tc2_print("FLAC", TC2_PRINT_WARNING,
		      "unknown FLAC metadata block type %i\n", fm.type);
	    break;
	}

	free(fm.data);
	if(fm.last)
	    break;
    }

    return err;
}

static void
flr_free(void *p)
{
    muxed_stream_t *ms = p;
    flacread_t *flr = ms->private;

    tcfree(flr->url);
    tcfree(flr->buf);
    free(flr->s.common.codec_data);
    free(flr);
}

extern muxed_stream_t *
flr_open(char *name, url_t *u, tcconf_section_t *conf, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;
    flacread_t *flr;
    u_char sig[4];

    if(u->read(sig, 1, 4, u) < 4)
	return NULL;

    if(memcmp(sig, "fLaC", 4))
	return NULL;

    flr = calloc(1, sizeof(*flr));
    flr->url = tcref(u);
    flr->s.stream_type = STREAM_TYPE_AUDIO;
    flr->s.audio.codec = "audio/flac";

    ms = tcallocdz(sizeof(*ms), NULL, flr_free);
    ms->streams = &flr->s;
    ms->used_streams = &flr->used;
    ms->n_streams = 1;
    ms->private = flr;
    ms->next_packet = flr_packet;

    if(flr_header(ms)){
	tcfree(ms);
	return NULL;
    }

    flr->bufsize = BUFSIZE;
    flr->buf = tcalloc(flr->bufsize);

    return ms;
}

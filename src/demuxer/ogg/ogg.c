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

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcbyteswap.h>
#include <ctype.h>
#include <ogg/ogg.h>
#include <ogg_tc2.h>

#define BUFFER_SIZE 8500
#define seek_fuzziness tcvp_demux_ogg_conf_seek_fuzziness

typedef struct {
    ogg_sync_state oy;
    ogg_stream_state os;    
    url_t *f;
    uint64_t end;
    uint64_t grp;
} ogg_stream_t;


/* Get the length of this stream, must be seekable */
static int64_t
ogg_get_length(muxed_stream_t *ms)
{
    ogg_stream_t *ost = ms->private;
    ogg_page og;
    url_t *f = ost->f;
    uint64_t more, pos;
    int l;
    uint64_t length;
    char *buf;


    f->seek(f, 0, SEEK_END);
    ost->end = f->tell(f);

    pos=ost->end-BUFFER_SIZE;
    f->seek(f, pos, SEEK_SET);

    ogg_sync_reset(&ost->oy);
    buf=ogg_sync_buffer(&ost->oy, BUFFER_SIZE);

    l = f->read(buf, 1, BUFFER_SIZE, ost->f);
    ogg_sync_wrote(&ost->oy, l);

    while((more=ogg_sync_pageseek(&ost->oy, &og))!=0)
    {
	if(more<0){
	    /* Skipping to start of page */
	    pos-=more;
	} else {
	    if(more+pos<ost->end){
		pos+=more;
	    } else {
		/* Found last page */
		break;
	    }
	}
	ogg_sync_pageout(&ost->oy, &og);
    }

    ogg_sync_pageout(&ost->oy, &og);

    length=ogg_page_granulepos(&og);

    return length;
}

static void
ogg_free_packet(void *v)
{
    packet_t *p = v;
    free(p->data[0]);
    free(p->data);
    free(p->sizes);
}


extern packet_t *
ogg_next_packet(muxed_stream_t *ms, int stream)
{
    packet_t *pk;

    ogg_stream_t *ost = ms->private;
    ogg_page og;
    ogg_packet op;
    char *buf;
    uint64_t gp = -1;

    while(ogg_stream_packetout(&ost->os, &op) != 1) {
	while(ogg_sync_pageout(&ost->oy, &og) != 1) {
	    int l;
	    buf = ogg_sync_buffer(&ost->oy, BUFFER_SIZE);
	    l = ost->f->read(buf, 1, BUFFER_SIZE, ost->f);
	    ogg_sync_wrote(&ost->oy, l);
	    if(l==0){
		return NULL;
	    }
	}
	ogg_stream_pagein(&ost->os, &og);
	gp = ost->grp;
	ost->grp = ogg_page_granulepos(&og);
    }

    pk = tcallocdz(sizeof(*pk), NULL, ogg_free_packet);
    pk->stream = 0;
    pk->flags = 0;
    if(gp != -1 && ms->streams[0].audio.sample_rate){
	pk->flags |= TCVP_PKT_FLAG_PTS;
	pk->pts = gp * 27000000 / ms->streams[0].audio.sample_rate;
    }
    pk->data = malloc(sizeof(*pk->data));
    pk->data[0] = malloc(sizeof(ogg_packet)+op.bytes);

    memcpy(pk->data[0], &op, sizeof(ogg_packet));
    memcpy(pk->data[0] + sizeof(ogg_packet), op.packet, op.bytes);
    ((ogg_packet *)pk->data[0])->packet = pk->data[0] + sizeof(ogg_packet);

    pk->sizes = malloc(sizeof(*pk->sizes));
    pk->sizes[0] = op.bytes+sizeof(ogg_packet);
    pk->planes = 1;

    return pk;
}

static uint64_t
ogg_seek(muxed_stream_t *ms, uint64_t time)
{
    ogg_page og;
    int l;
    char *buf;

    uint64_t pos = time * ms->streams[0].audio.sample_rate / 27000000;

    ogg_stream_t *ost = ms->private;
    url_t *f = ost->f;

    uint64_t start = 0;
    uint64_t end = ost->end;
    uint64_t ppos=0;
    uint64_t mid=0;

    while(end-start>seek_fuzziness) {
	mid = (start + end) / 2;

	f->seek(f, mid, SEEK_SET);

	ogg_sync_reset(&ost->oy);

	buf=ogg_sync_buffer(&ost->oy, BUFFER_SIZE);
	l=f->read(buf, 1, BUFFER_SIZE, f);
	ogg_sync_wrote(&ost->oy, l);

	while(ogg_sync_pageout(&ost->oy, &og) != 1)
	{
	    buf = ogg_sync_buffer(&ost->oy, BUFFER_SIZE);
	    l = f->read(buf, 1, BUFFER_SIZE, f);
	    ogg_sync_wrote(&ost->oy, l);
	}

	ppos=ogg_page_granulepos(&og);
	if(pos>ppos) {
	    start = mid+1;
	} else {
	    end = mid-1;
	}
    }

    f->seek(f, mid, SEEK_SET);
    ogg_sync_reset(&ost->oy);

    buf = ogg_sync_buffer(&ost->oy, BUFFER_SIZE);
    l = f->read(buf, 1, BUFFER_SIZE, f);
    ogg_sync_wrote(&ost->oy, l);
    ogg_sync_pageseek(&ost->oy, &og);

    return (27000000*ppos)/ms->streams[0].audio.sample_rate;
}

static void
ogg_free(void *p)
{
    muxed_stream_t *ms = p;
    ogg_stream_t *os = ms->private;

    ogg_sync_clear(&os->oy);
    ogg_stream_clear(&os->os);
    os->f->close(os->f);

    free(ms->streams);
    free(ms->used_streams);

    free(os);
}

/* Hack to fetch the title information without libvorbis. */
static int
ogg_title(muxed_stream_t *ms, char *buf, int size)
{
    char *p;
    int s, n, i, j;

    if(!(p = memmem(buf + 0x3b, size - 0x3b, "vorbis", 6)))
	return -1;

    p += 6;
    s = htol_32(unaligned32(p));

    p += s + 4;
    n = htol_32(unaligned32(p));
    p += 4;

    for(i = 0; i < n; i++){
	char *t, *v;
	int tl, vl;

	s = htol_32(unaligned32(p));
	t = p + 4;
	p += s + 4;

	v = memchr(t, '=', s);
	if(!v)
	    continue;

	tl = v - t;
	vl = s - tl - 1;
	v++;

	if(tl && vl){
	    char tt[tl];
	    char *ct;

	    for(j = 0; j < tl; j++)
		tt[j] = tolower(t[j]);

	    ct = malloc(vl + 1);
	    strncpy(ct, v, vl);
	    ct[vl] = 0;
	    tcattr_set(ms, tt, ct, NULL, free);
	}
    }

    return 0;
}

extern muxed_stream_t *
ogg_open(char *name, url_t *f, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    ogg_stream_t *ost;
    ogg_page og;
    char *buf;
    int l;
    muxed_stream_t *ms;

    ost = calloc(1, sizeof(*ost));

    ogg_sync_init(&ost->oy);

    ost->f = tcref(f);

    ms = tcallocd(sizeof(*ms), NULL, ogg_free);
    memset(ms, 0, sizeof(*ms));
    ms->n_streams = 1;
    ms->streams = calloc(1, sizeof(*ms->streams));

    ms->streams[0].stream_type = STREAM_TYPE_AUDIO;
    ms->streams[0].audio.codec = "audio/vorbis";

    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));
    ms->next_packet = ogg_next_packet;
    ms->seek = ogg_seek;
    ms->private = ost;

    if(!f->flags & URL_FLAG_STREAMED)
	ms->streams[0].audio.samples = ogg_get_length(ms);

    f->seek(ost->f, 0, SEEK_SET);
    ogg_sync_reset(&ost->oy);

    buf = ogg_sync_buffer(&ost->oy, BUFFER_SIZE);
    
    l = f->read(buf, 1, BUFFER_SIZE, ost->f);
    ogg_title(ms, buf, BUFFER_SIZE);

    ogg_sync_wrote(&ost->oy, l);
    ogg_sync_pageout(&ost->oy, &og);

//    ogg_sync_pageseek(&ost->oy, &og);

    ogg_stream_init(&ost->os, ogg_page_serialno(&og));
    ogg_stream_pagein(&ost->os, &og);

    return ms;
}

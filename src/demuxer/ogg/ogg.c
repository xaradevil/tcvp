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
#include <tclist.h>
#include <tcalloc.h>
#include <tcbyteswap.h>
#include <ctype.h>
#include <pthread.h>
#include <ogg/ogg.h>
#include <ogg_tc2.h>

#define BUFFER_SIZE 8500
#define seek_fuzziness tcvp_demux_ogg_conf_seek_fuzziness

typedef struct {
    ogg_sync_state oy;
    ogg_stream_state os;    
    url_t *f;
    uint64_t end;
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
ogg_free_packet(packet_t *p)
{
    free(p->data[0]);
    free(p->data);
    free(p->sizes);
    free(p);
}


extern packet_t *
ogg_next_packet(muxed_stream_t *ms, int stream)
{
    packet_t *pk;

    ogg_stream_t *ost = ms->private;
    ogg_page og;
    ogg_packet op;
    char *buf;

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

/* 	position in samples of the current page */
/*  	printf("%d\n",ogg_page_granulepos(&og)); */
    }

    pk = malloc(sizeof(*pk));
    pk->data = malloc(sizeof(*pk->data));
    pk->data[0] = malloc(sizeof(ogg_packet)+op.bytes);

    memcpy(pk->data[0], &op, sizeof(ogg_packet));
    memcpy(pk->data[0]+sizeof(ogg_packet), op.packet, op.bytes);
    ((ogg_packet *)pk->data[0])->packet=pk->data[0]+sizeof(ogg_packet);

    pk->free=ogg_free_packet;
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

    uint64_t pos = time * ms->streams[0].audio.sample_rate / 1000000;

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
/* 	fprintf(stderr, "filepos:%Ld streampos:%Ld %Ld\n", mid, ppos, pos); */
    }

    f->seek(f, mid, SEEK_SET);
    ogg_sync_reset(&ost->oy);

    buf = ogg_sync_buffer(&ost->oy, BUFFER_SIZE);
    l = f->read(buf, 1, BUFFER_SIZE, f);
    ogg_sync_wrote(&ost->oy, l);
    ogg_sync_pageseek(&ost->oy, &og);

    return (1000000*ppos)/ms->streams[0].audio.sample_rate;
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
    free(ms->file);
    if(ms->title)
	free(ms->title);
    if(ms->performer)
	free(ms->performer);

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
	    for(j = 0; j < tl; j++)
		tt[j] = tolower(t[j]);

	    if(!strncmp(tt, "title", tl)){
		ms->title = malloc(vl + 1);
		strncpy(ms->title, v, vl);
		ms->title[vl] = 0;
	    } else if(!strncmp(tt, "artist", tl)){
		ms->performer = malloc(vl + 1);
		strncpy(ms->performer, v, vl);
		ms->performer[vl] = 0;
	    }
	}
    }

    return 0;
}

extern muxed_stream_t *
ogg_open(char *name, conf_section *cs)
{
    ogg_stream_t *ost;
    ogg_page og;
    char *buf;
    int l;
    muxed_stream_t *ms;
    url_t *f;

    if(!(f = url_open(name, "r")))
	return NULL;

    ost=malloc(sizeof(ogg_stream_t));

    ogg_sync_init(&ost->oy);

    ost->f=f;

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

    ms->file = strdup(name);

    f->seek(ost->f, 0, SEEK_SET);
    ogg_sync_reset(&ost->oy);

    buf=ogg_sync_buffer(&ost->oy, BUFFER_SIZE);
    
    l=f->read(buf, 1, BUFFER_SIZE, ost->f);
    ogg_title(ms, buf, BUFFER_SIZE);

    ogg_sync_wrote(&ost->oy, l);
    ogg_sync_pageout(&ost->oy, &og);

//    ogg_sync_pageseek(&ost->oy, &og);

    ogg_stream_init(&ost->os, ogg_page_serialno(&og));
    ogg_stream_pagein(&ost->os, &og);

    return ms;
}

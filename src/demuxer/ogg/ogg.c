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
#include <pthread.h>
#include <ogg/ogg.h>
#include <ogg_tc2.h>

#define BUFFER_SIZE 8500

typedef struct {
    ogg_sync_state oy;
    ogg_stream_state os;    
    FILE *f;
} ogg_stream_t;


/* Get the length of this stream, must be seekable */
int64_t ogg_get_length(muxed_stream_t *ms)
{
    ogg_stream_t *ost = ms->private;
    ogg_page og;
    FILE *f = ost->f;
    long more, end, pos;
    int l;
    uint64_t length;
    char *buf;

    

    fseek(f, 0, SEEK_END);
    end=ftell(f);

    pos=end-BUFFER_SIZE;
    fseek(f, pos, SEEK_SET);

    ogg_sync_reset(&ost->oy);
    buf=ogg_sync_buffer(&ost->oy, BUFFER_SIZE);

    l=fread(buf, 1, BUFFER_SIZE, ost->f);
    ogg_sync_wrote(&ost->oy, l);

    while((more=ogg_sync_pageseek(&ost->oy, &og))!=0)
    {
	if(more<0){
	    /* Skipping to start of page */
	    pos-=more;
	} else {
	    if(more+pos<end){
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
    ogg_packet op, *p;
    char *buf;

    while(ogg_stream_packetout(&ost->os, &op) != 1) {
	while(ogg_sync_pageout(&ost->oy, &og) != 1) {
	    int l;
	    buf = ogg_sync_buffer(&ost->oy, BUFFER_SIZE);
	    l = fread(buf, 1, BUFFER_SIZE, ost->f);
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


extern int
ogg_close(muxed_stream_t *ms)
{
    ogg_stream_t *os = ms->private;
    int i;

    ogg_sync_clear(&os->oy);
    ogg_stream_clear(&os->os);
    fclose(os->f);

    free(ms->streams);
    free(ms->used_streams);

    free(os);

    return 0;
}


extern muxed_stream_t *
ogg_open(char *name)
{
    ogg_stream_t *ost;
    ogg_page og;
    char *buf;
    int l;
    muxed_stream_t *ms;

    ost=malloc(sizeof(ogg_stream_t));

    ogg_sync_init(&ost->oy);

    ost->f=fopen(name, "r");

    ms = malloc(sizeof(*ms));
    ms->n_streams = 1;
    ms->streams = malloc(sizeof(stream_t));

    ms->streams[0].stream_type = STREAM_TYPE_AUDIO;
    ms->streams[0].audio.codec = "audio/vorbis";

    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));
    ms->next_packet = ogg_next_packet;
    ms->close = ogg_close;
    ms->private = ost;

    ms->streams[0].audio.samples = ogg_get_length(ms);

    fseek(ost->f, 0, SEEK_SET);
    ogg_sync_reset(&ost->oy);

    buf=ogg_sync_buffer(&ost->oy, BUFFER_SIZE);
    
    l=fread(buf, 1, BUFFER_SIZE, ost->f);
    ogg_sync_wrote(&ost->oy, l);
    ogg_sync_pageout(&ost->oy, &og);

//    ogg_sync_pageseek(&ost->oy, &og);

    ogg_stream_init(&ost->os, ogg_page_serialno(&og));
    ogg_stream_pagein(&ost->os, &og);

    return ms;
}

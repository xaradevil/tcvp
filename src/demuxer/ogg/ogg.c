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

#define BUFFER_SIZE 4096

typedef struct {
    ogg_sync_state oy;
    ogg_stream_state os;    
    FILE *f;
} ogg_stream_t;


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
	    l = fread(buf, BUFFER_SIZE, 1, ost->f);
	    ogg_sync_wrote(&ost->oy, l);
	    if(l==0){
		return NULL;
	    }
	}
	ogg_stream_pagein(&ost->os, &og);
    }

    pk = malloc(sizeof(*pk));
    pk->data = malloc(sizeof(*pk->data));
    pk->data[0] = malloc(sizeof(ogg_packet)+op.bytes);

    memcpy(pk->data[0], &op, sizeof(ogg_packet));
    memcpy(pk->data[0]+sizeof(ogg_packet), op.packet, op.bytes);
    ((ogg_packet *)pk->data[0])->packet=pk->data[0]+sizeof(ogg_packet);

    pk->free=ogg_free_packet;
    pk->sizes = malloc(sizeof(size_t));
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
    muxed_stream_t *ms;

    ost=malloc(sizeof(ogg_stream_t));

    ogg_sync_init(&ost->oy);

    buf=ogg_sync_buffer(&ost->oy, BUFFER_SIZE);
    
    ost->f=fopen(name, "r");

    fread(buf, BUFFER_SIZE, 1, ost->f);

    ogg_sync_wrote(&ost->oy, BUFFER_SIZE);
    ogg_sync_pageout(&ost->oy, &og);
    ogg_stream_init(&ost->os, ogg_page_serialno(&og));
    ogg_stream_pagein(&ost->os, &og);


    ms = malloc(sizeof(*ms));
    ms->n_streams = 1;
    ms->streams = malloc(sizeof(stream_t));

    ms->streams[0].stream_type = STREAM_TYPE_AUDIO;
    ms->streams[0].audio.sample_rate = 44100;
    ms->streams[0].audio.channels = 2;
    ms->streams[0].audio.codec = "audio/vorbis";

    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));
    ms->next_packet = ogg_next_packet;
    ms->close = ogg_close;
    ms->private = ost;

    return ms;
}


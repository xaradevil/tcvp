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
#include <tcvp.h>
#include <tcvp_tc2.h>

static tcvp_pipe_t *demux, *codec, *sound;
static muxed_stream_t *stream;

extern int
tcvp_init(char *arg)
{
    int i;
    char buf[64];
    codec_new_t cnew;

    if((stream = video_open(arg)) == NULL)
	return -1;

    for(i = 0; i < stream->n_streams; i++){
	printf("Stream %i, ", i);
	switch(stream->streams[i].stream_type){
	case STREAM_TYPE_AUDIO:
	    printf("%s, %i Hz, %i channels\n",
		   stream->streams[i].audio.codec,
		   stream->streams[i].audio.sample_rate,
		   stream->streams[i].audio.channels);
	    break;
	case STREAM_TYPE_VIDEO:
	    printf("%s, %ix%i, %i fps\n",
		   stream->streams[i].video.codec,
		   stream->streams[i].video.width,
		   stream->streams[i].video.height,
		   stream->streams[i].video.frame_rate);
	    break;
	}
    }

    for(i = 0; i < stream->n_streams; i++){
	if(stream->streams[i].stream_type == STREAM_TYPE_AUDIO){
	    break;
	}
    }

    if(i == stream->n_streams)
	return 0;

    stream->used_streams[i] = 1;

    sprintf(buf, "codec/%s", stream->streams[i].audio.codec);
    if((cnew = tc2_get_symbol(buf, "new")) == NULL){
	fprintf(stderr, "TCVP: Can't load %s\n", buf);
	return 0;
    }

/*     fprintf(stderr, "%s:%i\n", __FILE__, __LINE__); */
    sound = output_audio_open((audio_stream_t *)&stream->streams[i], NULL);
/*     fprintf(stderr, "%s:%i\n", __FILE__, __LINE__); */
    codec = cnew(sound);
/*     fprintf(stderr, "%s:%i\n", __FILE__, __LINE__); */
    demux = video_play(stream, &codec);
/*     fprintf(stderr, "%s:%i\n", __FILE__, __LINE__); */

    demux->start(demux);
/*     fprintf(stderr, "%s:%i\n", __FILE__, __LINE__); */
/*     sound->start(sound); */
/*     fprintf(stderr, "%s:%i\n", __FILE__, __LINE__); */

    return 0;
}

extern int
tcvp_stop(void)
{
    if(demux)
	demux->free(demux);

    if(codec)
	codec->free(codec);

    if(sound)
	sound->free(sound);

    if(stream)
	stream->close(stream);

    return 0;
}

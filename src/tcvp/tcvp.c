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

static int tcvp_stop(char *p);

static int
tcvp_play(char *arg)
{
    int i;
    char buf[64];
    codec_new_pipe_t cnew;

    tcvp_stop(NULL);

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
	    printf("%s, %ix%i, %f fps\n",
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
	return -1;

    stream->used_streams[i] = 1;

    sprintf(buf, "codec/%s", stream->streams[i].audio.codec);
    if((cnew = tc2_get_symbol(buf, "new_pipe")) == NULL){
	fprintf(stderr, "TCVP: Can't load %s\n", buf);
	return -1;
    }

    sound = output_audio_open((audio_stream_t *)&stream->streams[i], NULL);
    codec = cnew(CODEC_MODE_DECODE, sound);
    demux = video_play(stream, &codec);

    demux->start(demux);

    return 0;
}

static int
tcvp_pause(char *p)
{
    static int paused = 0;

    if(!sound)
	return -1;

    if(paused ^= 1)
	sound->stop(sound);
    else
	sound->start(sound);

    return 0;
}

static int
tcvp_stop(char *p)
{
    if(demux){
	demux->free(demux);
	demux = NULL;
    }

    if(codec){
	codec->free(codec);
	codec = NULL;
    }

    if(sound){
	sound->free(sound);
	sound = NULL;
    }

    if(stream){
	stream->close(stream);
	stream = NULL;
    }

    return 0;
}

static command *play_cmd, *pause_cmd, *stop_cmd;

extern int
tcvp_init(char *p)
{
    play_cmd = malloc(sizeof(command));
    play_cmd->name = strdup("play");
    play_cmd->cmd_fn = tcvp_play;
    shell_register_command(play_cmd);

    pause_cmd = malloc(sizeof(command));
    pause_cmd->name = strdup("pause");
    pause_cmd->cmd_fn = tcvp_pause;
    shell_register_command(pause_cmd);

    stop_cmd = malloc(sizeof(command));
    stop_cmd->name = strdup("stop");
    stop_cmd->cmd_fn = tcvp_stop;
    shell_register_command(stop_cmd);

    shell_register_prompt("TCVP$ ");

    return 0;
}

extern int
tcvp_shdn(void)
{
    tcvp_stop(NULL);
    shell_unregister_command(play_cmd);
    shell_unregister_command(pause_cmd);
    shell_unregister_command(stop_cmd);
    shell_unregister_prompt();

    return 0;
}

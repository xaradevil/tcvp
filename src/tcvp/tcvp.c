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

static tcvp_pipe_t *demux, *vcodec, *acodec, *sound, *video;
static muxed_stream_t *stream;
timer__t *timer;

static int tcvp_stop(char *p);

static int
tcvp_play(char *arg)
{
    int i, j;
    char buf[64];
    codec_new_t acnew = NULL, vcnew = NULL;
    timer_new_t tmnew;
    stream_t *as = NULL, *vs = NULL;
    tcvp_pipe_t *codecs[2];
    int aci, vci;

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

    for(i = 0, j = 0; i < stream->n_streams; i++){
	if(stream->streams[i].stream_type == STREAM_TYPE_VIDEO && !vs){
	    vs = &stream->streams[i];
	    sprintf(buf, "codec/%s", vs->video.codec);
	    if((vcnew = tc2_get_symbol(buf, "new")) == NULL){
		fprintf(stderr, "TCVP: Can't load %s\n", buf);
		vs = NULL;
		continue;
	    }
	    stream->used_streams[i] = 1;
	    vci = j++;
	} else if(stream->streams[i].stream_type == STREAM_TYPE_AUDIO &&
		  !as && output_audio_open){
	    as = &stream->streams[i];
	    sprintf(buf, "codec/%s", as->audio.codec);
	    if((acnew = tc2_get_symbol(buf, "new")) == NULL){
		fprintf(stderr, "TCVP: Can't load %s\n", buf);
		as = NULL;
		continue;
	    }
	    stream->used_streams[i] = 1;
	    aci = j++;
	}
    }

    if(!as && !vs)
	return -1;

    if(as && acnew && output_audio_open){
	sound = output_audio_open((audio_stream_t *) as, NULL, &timer);
	acodec = acnew(as, CODEC_MODE_DECODE, sound);
	codecs[aci] = acodec;
    }

    if(vs && vcnew){
	if(!timer){
	    tmnew = tc2_get_symbol("timer", "new");
	    timer = tmnew(10000);
	}
	video = output_video_open((video_stream_t *) vs, NULL, timer);
	vcodec = vcnew(vs, CODEC_MODE_DECODE, video);
	codecs[vci] = vcodec;
    }

    demux = video_play(stream, codecs);

    demux->start(demux);
    if(timer)
	timer->start(timer);

    return 0;
}

static int
tcvp_pause(char *p)
{
    static int paused = 0;

    if(!sound && !timer)
	return -1;

    if(paused ^= 1){
	if(sound)
	    sound->stop(sound);
	if(timer)
	    timer->stop(timer);
    } else {
	if(sound)
	    sound->start(sound);
	if(timer)
	    timer->start(timer);
    }

    return 0;
}

static int
tcvp_stop(char *p)
{
    if(demux){
	demux->free(demux);
	demux = NULL;
    }

    if(acodec){
	acodec->free(acodec);
	acodec = NULL;
    }

    if(vcodec){
	vcodec->free(vcodec);
	vcodec = NULL;
    }

    if(video){
	video->free(video);
	video = NULL;
    }

    if(sound){
	sound->free(sound);
	sound = NULL;
    }

    if(stream){
	stream->close(stream);
	stream = NULL;
    }

    if(timer){
	timer->free(timer);
	timer = NULL;
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

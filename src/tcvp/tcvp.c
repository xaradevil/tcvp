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
#include <tcvp_types.h>
#include <tcvp_tc2.h>

typedef struct tcvp_player {
    tcvp_pipe_t *demux, *vcodec, *acodec, *sound, *video;
    muxed_stream_t *stream;
    timer__t *timer;
} tcvp_player_t;

static int
t_start(player_t *pl)
{
    tcvp_player_t *tp = pl->private;

    if(tp->video)
	tp->video->start(tp->video);

    if(tp->sound)
	tp->sound->start(tp->sound);

    if(tp->timer)
	tp->timer->start(tp->timer);

    return 0;
}

static int
t_stop(player_t *pl)
{
    tcvp_player_t *tp = pl->private;

    if(tp->timer)
	tp->timer->stop(tp->timer);

    if(tp->sound)
	tp->sound->stop(tp->sound);

    if(tp->video)
	tp->video->stop(tp->video);

    return 0;
}

static int
t_close(player_t *pl)
{
    tcvp_player_t *tp = pl->private;

    if(tp->demux)
	tp->demux->free(tp->demux);

    if(tp->acodec)
	tp->acodec->free(tp->acodec);

    if(tp->vcodec)
	tp->vcodec->free(tp->vcodec);

    if(tp->video)
	tp->video->free(tp->video);

    if(tp->sound)
	tp->sound->free(tp->sound);

    if(tp->stream)
	tp->stream->close(tp->stream);

    if(tp->timer)
	tp->timer->free(tp->timer);

    free(tp);
    free(pl);

    return 0;
}

extern player_t *
t_open(char *name)
{
    int i, j;
    char buf[64];
    codec_new_t acnew = NULL, vcnew = NULL;
    timer_new_t tmnew;
    stream_t *as = NULL, *vs = NULL;
    tcvp_pipe_t *codecs[2];
    int aci, vci;
    tcvp_pipe_t *demux = NULL;
    tcvp_pipe_t *vcodec = NULL, *acodec = NULL;
    tcvp_pipe_t *sound = NULL, *video = NULL;
    muxed_stream_t *stream = NULL;
    timer__t *timer = NULL;
    tcvp_player_t *tp;
    player_t *pl;

    if((stream = stream_open(name)) == NULL)
	return NULL;

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
	return NULL;

    if(as){
	sound = output_audio_open((audio_stream_t *) as, NULL, &timer);
	acodec = acnew(as, CODEC_MODE_DECODE, sound);
	codecs[aci] = acodec;
    }

    if(vs){
	if(!timer){
	    tmnew = tc2_get_symbol("timer", "new");
	    timer = tmnew(10000);
	}
	video = output_video_open((video_stream_t *) vs, NULL, timer);
	vcodec = vcnew(vs, CODEC_MODE_DECODE, video);
	codecs[vci] = vcodec;
    }

    demux = stream_play(stream, codecs);

    tp = malloc(sizeof(*tp));
    tp->demux = demux;
    tp->vcodec = vcodec;
    tp->acodec = acodec;
    tp->sound = sound;
    tp->video = video;
    tp->stream = stream;
    tp->timer = timer;

    pl = malloc(sizeof(*pl));
    pl->start = t_start;
    pl->stop = t_stop;
    pl->seek = NULL;
    pl->close = t_close;
    pl->private = tp;

    demux->start(demux);

    return pl;
}

/* Shell stuff below.  To be moved. */

static player_t *player;

static int
tcvp_pause(char *p)
{
    static int paused = 0;

    if(!player)
	return -1;

    (paused ^= 1)? player->stop(player): player->start(player);

    return 0;
}

static int
tcvp_stop(char *p)
{
    if(player){
	player->close(player);
	player = NULL;
    }

    return 0;
}

static int
tcvp_play(char *file)
{
    tcvp_stop(NULL);

    if(!(player = t_open(file)))
	return -1;

    player->start(player);

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

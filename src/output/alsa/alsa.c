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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>

#define ALSA_PCM_NEW_HW_PARAMS_API 1
#include <alsa/asoundlib.h>
#include <alsa/pcm_plugin.h>

#include <tcvp_types.h>
#include <alsa_tc2.h>
#include <alsamod.h>

typedef struct alsa_out {
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *hwp;
    int bpf;
    timer__t *timer;
    int state;
    pthread_mutex_t mx;
    pthread_cond_t cd;
} alsa_out_t;

static int
alsa_start(tcvp_pipe_t *p)
{
    alsa_out_t *ao = p->private;

    pthread_mutex_lock(&ao->mx);
    ao->state = RUN;
    pthread_cond_broadcast(&ao->cd);
    pthread_mutex_unlock(&ao->mx);
    if(snd_pcm_state(ao->pcm) == SND_PCM_STATE_PAUSED)
	snd_pcm_pause(ao->pcm, 0);

    return 0;
}

static int
alsa_stop(tcvp_pipe_t *p)
{
    alsa_out_t *ao = p->private;

    ao->state = PAUSE;
    if(snd_pcm_state(ao->pcm) == SND_PCM_STATE_RUNNING)
	snd_pcm_pause(ao->pcm, 1);

    return 0;
}

static int
alsa_free(tcvp_pipe_t *p)
{
    alsa_out_t *ao = p->private;

    snd_pcm_drop(ao->pcm);
    snd_pcm_close(ao->pcm);
    snd_pcm_hw_params_free(ao->hwp);
    tm_stop(ao->timer);
    free(ao);
    free(p);

    return 0;
}

static int
alsa_flush(tcvp_pipe_t *p, int drop)
{
    alsa_out_t *ao = p->private;

    if(drop)
	snd_pcm_drop(ao->pcm);
    else
	snd_pcm_drain(ao->pcm);

    ao->timer->interrupt(ao->timer);

    return 0;
}

static int
alsa_play(tcvp_pipe_t *p, packet_t *pk)
{
    alsa_out_t *ao = p->private;
    size_t count = pk->sizes[0] / ao->bpf;
    u_char *data = pk->data[0];

    pthread_mutex_lock(&ao->mx);
    while(ao->state == PAUSE){
	pthread_cond_wait(&ao->cd, &ao->mx);
    }
    pthread_mutex_unlock(&ao->mx);

    while(count > 0){
	int r = snd_pcm_writei(ao->pcm, data, count);
	if (r == -EAGAIN || (r >= 0 && r < count)) {
	    snd_pcm_wait(ao->pcm, 1000);
	} else if(r == -EPIPE){
	    fprintf(stderr, "ALSA: xrun\n");
	    snd_pcm_prepare(ao->pcm);
	} else if(r < 0){
	    fprintf(stderr, "ALSA: write error: %s\n", snd_strerror(r));
	    return -1;
	}
	if(r > 0){
	    count -= r;
	    data += r * ao->bpf;
	}
    }

    pk->free(pk);

    return 0;
}

static snd_pcm_route_ttable_entry_t ttable_6_2[] = {
    SND_PCM_PLUGIN_ROUTE_FULL/4, SND_PCM_PLUGIN_ROUTE_FULL/4,
    SND_PCM_PLUGIN_ROUTE_FULL/4, 0,
    SND_PCM_PLUGIN_ROUTE_FULL/4, SND_PCM_PLUGIN_ROUTE_FULL/4,
    0, SND_PCM_PLUGIN_ROUTE_FULL/4,
    SND_PCM_PLUGIN_ROUTE_FULL/4, 0,
    0, SND_PCM_PLUGIN_ROUTE_FULL/4
};

static snd_pcm_route_ttable_entry_t *ttables[7][7] = {
    [2][6] = ttable_6_2
};

extern tcvp_pipe_t *
alsa_open(audio_stream_t *as, conf_section *cs, timer__t **timer)
{
    tcvp_pipe_t *tp;
    alsa_out_t *ao;
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *hwp;
    u_int rate = as->sample_rate, channels = as->channels, ptime;
    int tmp;
    char *device = NULL;

    if(cs)
	conf_getvalue(cs, "audio/device", "%s", &device);

    if(!device)
	device = "hw:0,0";

    if(snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0) != 0)
	return NULL;

    snd_pcm_hw_params_malloc(&hwp);
    snd_pcm_hw_params_any(pcm, hwp);

    snd_pcm_hw_params_set_access(pcm, hwp,
				 SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hwp, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(pcm, hwp, &rate, &tmp);
    snd_pcm_hw_params_set_channels_near(pcm, hwp, &channels);

    ptime = 10000;
    snd_pcm_hw_params_set_period_time_near(pcm, hwp, &ptime, &tmp);

    snd_pcm_hw_params(pcm, hwp);

    if(timer)
	*timer = open_timer(pcm);

    if(channels != as->channels){
	snd_pcm_t *rpcm;
	snd_pcm_hw_params_t *rp;

	if(!ttables[channels][as->channels]){
	    snd_pcm_close(pcm);
	    return NULL;
	}

	snd_pcm_route_open(&rpcm, "default", SND_PCM_FORMAT_S16_LE,
			   channels, ttables[channels][as->channels],
			   channels, as->channels, channels, pcm, 1);
	snd_pcm_hw_params_alloca(&rp);
	snd_pcm_hw_params_any(rpcm, rp);

	snd_pcm_hw_params_set_access(rpcm, rp,
				     SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(rpcm, rp, SND_PCM_FORMAT_S16_LE);
	snd_pcm_hw_params_set_rate_near(rpcm, rp, &rate, &tmp);
	snd_pcm_hw_params_set_channels(rpcm, rp, as->channels);

	snd_pcm_hw_params(rpcm, rp);

	pcm = rpcm;
    }

    snd_pcm_prepare(pcm);

    ao = calloc(1, sizeof(*ao));
    ao->pcm = pcm;
    ao->hwp = hwp;
    ao->bpf = as->channels * 2;
    pthread_mutex_init(&ao->mx, NULL);
    pthread_cond_init(&ao->cd, NULL);
    ao->state = PAUSE;
    if(timer)
	ao->timer = *timer;

    tp = calloc(1, sizeof(*tp));
    tp->input = alsa_play;
    tp->start = alsa_start;
    tp->stop = alsa_stop;
    tp->free = alsa_free;
    tp->flush = alsa_flush;
    tp->private = ao;

    return tp;
}

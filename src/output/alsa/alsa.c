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

#define ALSA_PCM_NEW_HW_PARAMS_API 1
#include <alsa/asoundlib.h>
#include <alsa/pcm_plugin.h>

#include <tcvp.h>
#include <alsa_tc2.h>

typedef struct alsa_out {
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *hwp;
    int bpf;
} alsa_out_t;

static int
alsa_start(tcvp_pipe_t *p)
{
    alsa_out_t *ao = p->private;

    snd_pcm_pause(ao->pcm, 0);

    return 0;
}

static int
alsa_stop(tcvp_pipe_t *p)
{
    alsa_out_t *ao = p->private;

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
    free(ao);
    free(p);

    return 0;
}

static int
alsa_play(tcvp_pipe_t *p, packet_t *pk)
{
    alsa_out_t *ao = p->private;

    snd_pcm_writei(ao->pcm, pk->data[0], pk->sizes[0] / ao->bpf);
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
alsa_open(audio_stream_t *as, char *device)
{
    tcvp_pipe_t *tp;
    alsa_out_t *ao;
    snd_pcm_t *pcm, *rpcm;
    snd_pcm_hw_params_t *hwp;
    u_int rate = as->sample_rate, channels = as->channels;
    int tmp;

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

    snd_pcm_hw_params_set_period_size(pcm, hwp, 4096, 0);
    snd_pcm_hw_params_set_periods(pcm, hwp, 4, 0);

    snd_pcm_hw_params(pcm, hwp);

    if(channels != as->channels){
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

	snd_pcm_hw_params_set_period_size(rpcm, rp, 4096, 0);
	snd_pcm_hw_params_set_periods(rpcm, rp, 4, 0);

	snd_pcm_hw_params(rpcm, rp);

	pcm = rpcm;
    }

    snd_pcm_prepare(pcm);

    ao = malloc(sizeof(*ao));
    ao->pcm = pcm;
    ao->hwp = hwp;
    ao->bpf = as->channels * 2;

    tp = malloc(sizeof(*tp));
    tp->input = alsa_play;
    tp->start = alsa_start;
    tp->stop = alsa_stop;
    tp->free = alsa_free;
    tp->private = ao;

    return tp;
}

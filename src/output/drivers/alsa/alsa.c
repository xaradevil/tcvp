/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

    Licensed under the Open Software License version 2.0
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
#include <tcalloc.h>

#define ALSA_PCM_NEW_HW_PARAMS_API 1
#include <alsa/asoundlib.h>
#include <alsa/pcm_plugin.h>

#include <tcvp_types.h>
#include <alsa_tc2.h>
#include <alsamod.h>

typedef struct alsa_out {
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *hwp;
    tcvp_timer_t *timer;
    timer_driver_t *tmdrivers[2];
    int can_pause;
} alsa_out_t;

static int
alsa_start(audio_driver_t *p)
{
    alsa_out_t *ao = p->private;
    int s;

    s = snd_pcm_state(ao->pcm);
    if(s == SND_PCM_STATE_PAUSED)
	snd_pcm_pause(ao->pcm, 0);
    else if(s != SND_PCM_STATE_RUNNING && s != SND_PCM_STATE_PREPARED)
	snd_pcm_prepare(ao->pcm);

    return 0;
}

static int
alsa_stop(audio_driver_t *p)
{
    alsa_out_t *ao = p->private;

    if(ao->can_pause && (snd_pcm_state(ao->pcm) == SND_PCM_STATE_RUNNING))
	snd_pcm_pause(ao->pcm, 1);

    return 0;
}

static void
alsa_free(void *p)
{
    audio_driver_t *ad = p;
    alsa_out_t *ao = ad->private;

    if(ao->hwp){
	snd_pcm_hw_params_free(ao->hwp);
	snd_pcm_drop(ao->pcm);
    }
    if(ao->pcm){
	snd_pcm_close(ao->pcm);
    }

    if(ao->tmdrivers[PCM])
	tcfree(ao->tmdrivers[PCM]);
    if(ao->tmdrivers[SYSTEM]){
	ao->timer->set_driver(ao->timer, tcref(ao->tmdrivers[SYSTEM]));
	tcfree(ao->tmdrivers[SYSTEM]);
    }
    free(ao);
}

static int
alsa_flush(audio_driver_t *p, int drop)
{
    alsa_out_t *ao = p->private;

    if(drop){
	if(ao->hwp)
	    snd_pcm_drop(ao->pcm);
    } else {
	snd_pcm_drain(ao->pcm);
    }

    return 0;
}

static int
alsa_write(audio_driver_t *ad, void *data, int samples)
{
    alsa_out_t *ao = ad->private;
    int r;

    r = snd_pcm_writei(ao->pcm, data, samples);

    if(r < 0 && r != -EAGAIN){
	if(snd_pcm_prepare(ao->pcm) < 0){
	    fprintf(stderr, "ALSA: %s\n", snd_strerror(r));
	} else {
	    r = -EAGAIN;
	}
    }

    return r;
}

static int
alsa_wait(audio_driver_t *ad, int timeout)
{
    alsa_out_t *ao = ad->private;
    snd_pcm_wait(ao->pcm, timeout);
    return 0;
}

static int
alsa_delay(audio_driver_t *ad)
{
    alsa_out_t *ao = ad->private;
    snd_pcm_sframes_t df;
    snd_pcm_delay(ao->pcm, &df);
    return df;
}

extern audio_driver_t *
alsa_new(audio_stream_t *as, tcconf_section_t *cs, tcvp_timer_t *timer)
{
    audio_driver_t *ad;
    alsa_out_t *ao;
    snd_pcm_hw_params_t *hwp;
    snd_pcm_t *pcm;
    u_int rate = as->sample_rate, channels = as->channels, ptime;
    int tmp = 0;
    char *format;
    char *device = NULL;

    tcconf_getvalue(cs, "audio/device", "%s", &device);
    if(!device){
	if(tcvp_driver_audio_alsa_conf_device)
	    device = strdup(tcvp_driver_audio_alsa_conf_device);
	else
	    return NULL;
    }

    if(snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)){
	fprintf(stderr, "ALSA: Can't open device '%s'\n", device);
	return NULL;
    }

    free(device);
    snd_config_update_free_global();

    snd_pcm_hw_params_malloc(&hwp);
    snd_pcm_hw_params_any(pcm, hwp);

    snd_pcm_hw_params_set_access(pcm, hwp, SND_PCM_ACCESS_RW_INTERLEAVED);

    if(strstr(as->codec, "pcm-s16")){
	snd_pcm_hw_params_set_format(pcm, hwp, SND_PCM_FORMAT_S16_LE);
	format = "s16le";
    } else if(strstr(as->codec, "pcm-u16")){
	snd_pcm_hw_params_set_format(pcm, hwp, SND_PCM_FORMAT_U16_LE);
	format = "u16le";
    } else if(strstr(as->codec, "pcm-u8")){
	snd_pcm_hw_params_set_format(pcm, hwp, SND_PCM_FORMAT_U8);
	format = "u8";
    } else {
	fprintf(stderr, "ALSA: unsupported format %s\n", as->codec);
	goto err;
    }

    snd_pcm_hw_params_set_rate_near(pcm, hwp, &rate, &tmp);
    snd_pcm_hw_params_set_channels_near(pcm, hwp, &channels);

    ptime = 10000;
    snd_pcm_hw_params_set_period_time_near(pcm, hwp, &ptime, &tmp);

    if(snd_pcm_hw_params(pcm, hwp) < 0){
	fprintf(stderr, "ALSA: snd_pcm_hw_parameters failed\n");
	goto err;
    }

    ao = calloc(1, sizeof(*ao));
    ao->pcm = pcm;
    ao->timer = timer;
    ao->tmdrivers[PCM] = open_timer(270000, pcm);
    ao->tmdrivers[SYSTEM] = open_timer(270000, NULL);
    timer->set_driver(timer, tcref(ao->tmdrivers[PCM]));
    ao->hwp = hwp;
    ao->can_pause = snd_pcm_hw_params_can_pause(hwp);

    ad = tcallocdz(sizeof(*ad), NULL, alsa_free);
    ad->format = format;
    ad->write = alsa_write;
    ad->wait = alsa_wait;
    ad->delay = alsa_delay;
    ad->start = alsa_start;
    ad->stop = alsa_stop;
    ad->flush = alsa_flush;
    ad->private = ao;

    return ad;
err:
    snd_pcm_hw_params_free(hwp);
    snd_pcm_close(pcm);
    return NULL;
}

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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <oss_tc2.h>

typedef struct oss_out {
    int fd;
    int state;
    pthread_mutex_t mx;
    pthread_cond_t cd;
} oss_out_t;

#define RUN   1
#define PAUSE 2
#define STOP  3

static int
oss_start(tcvp_pipe_t *p)
{
    oss_out_t *ao = p->private;

    pthread_mutex_lock(&ao->mx);
    ao->state = RUN;
    pthread_cond_broadcast(&ao->cd);
    pthread_mutex_unlock(&ao->mx);
    

    return 0;
}

static int
oss_stop(tcvp_pipe_t *p)
{
    oss_out_t *ao = p->private;

    ao->state = PAUSE;

    return 0;
}

static void
oss_free(void *p)
{
    tcvp_pipe_t *tp = p;
    oss_out_t *ao = tp->private;

    close(ao->fd);
    free(ao);
    free(p);
}

static int
oss_flush(tcvp_pipe_t *p, int d)
{
    return 0;
}

static int
oss_play(tcvp_pipe_t *p, packet_t *pk)
{
    oss_out_t *ao = p->private;
    size_t count;
    u_char *data;

    if(!pk->data){
	pk->free(pk);
	return 0;
    }

    count = pk->sizes[0];
    data = pk->data[0];

    pthread_mutex_lock(&ao->mx);
    while(ao->state == PAUSE)
	pthread_cond_wait(&ao->cd, &ao->mx);
    pthread_mutex_unlock(&ao->mx);

    while(count > 0){
	int r = write(ao->fd, data, count);
	if(r < 0){
	    fprintf(stderr, "OSS: write error: %s\n", strerror(r));
	    return -1;
	}
	count -= r;
	data += r;
    }

    pk->free(pk);

    return 0;
}

extern tcvp_pipe_t *
oss_open(audio_stream_t *as, conf_section *cs, timer__t **timer)
{
    tcvp_pipe_t *tp;
    oss_out_t *ao;
    int dsp;
    u_int rate = as->sample_rate, channels = as->channels;
    u_int format = AFMT_S16_LE;
    char *device = NULL;

    if(cs)
	conf_getvalue(cs, "audio/device", "%s", &device);

    if(!device)
	device = "/dev/dsp";

    if((dsp = open(device, O_WRONLY)) < 0){
	perror(device);
	return NULL;
    }

    if(ioctl(dsp, SNDCTL_DSP_SETFMT, &format) == -1){
	perror("ioctl");
	close(dsp);
	return NULL;
    }

    if(ioctl(dsp, SNDCTL_DSP_CHANNELS, &channels) == -1){
	perror("ioctl");
	close(dsp);
	return NULL;
    }

    if(ioctl(dsp, SNDCTL_DSP_SPEED, &rate) == -1){
	perror("ioctl");
	close(dsp);
	return NULL;
    }

    if(channels != as->channels){
	fprintf(stderr, "OSS: %i channels not supported.\n", as->channels);
    }

    if(rate != as->sample_rate){
	fprintf(stderr, "OSS: %i Hz sample rate not supported.\n",
		as->sample_rate);
    }

    ao = malloc(sizeof(*ao));
    ao->fd = dsp;
    ao->state = PAUSE;
    pthread_mutex_init(&ao->mx, NULL);
    pthread_cond_init(&ao->cd, NULL);

    tp = tcallocdz(sizeof(*tp), NULL, oss_free);
    tp->input = oss_play;
    tp->start = oss_start;
    tp->stop = oss_stop;
    tp->flush = oss_flush;
    tp->private = ao;

    return tp;
}

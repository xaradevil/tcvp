/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/time.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <oss_tc2.h>

typedef struct oss_out {
    int fd;
    int ssize;
    int format;
    int channels;
    int rate;
    char *device;
} oss_out_t;

#define min(a, b)  ((a) < (b)? (a): (b))

static int
oss_reset(oss_out_t *oo)
{
    if(oo->fd >= 0)
	close(oo->fd);
    oo->fd = open(oo->device, O_WRONLY);
    ioctl(oo->fd, SNDCTL_DSP_SETFMT, &oo->format);
    ioctl(oo->fd, SNDCTL_DSP_CHANNELS, &oo->channels);
    ioctl(oo->fd, SNDCTL_DSP_SPEED, &oo->rate);
    return 0;
}

static int
oss_start(audio_driver_t *ad)
{
    return 0;
}

static int
oss_stop(audio_driver_t *ad)
{
    oss_out_t *oo = ad->private;
    ioctl(oo->fd, SNDCTL_DSP_POST, 0);
    return 0;
}

static void
oss_free(void *p)
{
    audio_driver_t *ad = p;
    oss_out_t *oo = ad->private;
    close(oo->fd);
    free(oo->device);
    free(oo);
}

static int
oss_flush(audio_driver_t *ad, int d)
{
    oss_out_t *oo = ad->private;
    ioctl(oo->fd, d? SNDCTL_DSP_POST: SNDCTL_DSP_SYNC, 0);
    return 0;
}

static int
oss_write(audio_driver_t *ad, void *data, int samples)
{
    oss_out_t *oo = ad->private;
    int r;

    struct audio_buf_info abi;
    struct timeval tv = { 0, 0 };
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(oo->fd, &fds);

    if(select(FD_SETSIZE, NULL, &fds, NULL, &tv) <= 0)
	return -EAGAIN;

    if(ioctl(oo->fd, SNDCTL_DSP_GETOSPACE, &abi) < 0){
	oss_reset(oo);
	return -EAGAIN;
    }

    r = min(abi.fragments * abi.fragsize, samples * oo->ssize);
    if(r > 0){
	r = write(oo->fd, data, r);
	if(r < 0)
	    r = -errno;
	else
	    r /= oo->ssize;
    } else {
	r = -EAGAIN;
    }

    return r;
}

static int
oss_delay(audio_driver_t *ad)
{ 
/*     oss_out_t *oo = ad->private; */
    int d = 0;
    /* Sync seems to be better without this.  Why? */
/*     ioctl(oo->fd, SNDCTL_DSP_GETODELAY, &d); */
    return d;
}

static int
oss_wait(audio_driver_t *ad, int timeout)
{
    oss_out_t *oo = ad->private;
    struct timeval tv = { timeout / 1000, (timeout % 1000) * 1000 };
    fd_set fd;

    FD_ZERO(&fd);
    FD_SET(oo->fd, &fd);
    select(FD_SETSIZE, NULL, &fd, NULL, &tv);

    return 0;
}

extern audio_driver_t *
oss_new(audio_stream_t *as, tcconf_section_t *cs, tcvp_timer_t *timer)
{
    audio_driver_t *ad;
    oss_out_t *oo;
    struct audio_buf_info abi;
    int dsp;
    u_int rate = as->sample_rate, channels = as->channels;
    u_int ofmt;
    char *format;
    char *device = NULL;
    int ssize;

    tcconf_getvalue(cs, "audio/device", "%s", &device);
    if(!device){
	if(tcvp_driver_audio_oss_conf_device)
	    device = strdup(tcvp_driver_audio_oss_conf_device);
	else
	    return NULL;
    }

    if((dsp = open(device, O_WRONLY | O_NONBLOCK)) < 0){
	perror(device);
	return NULL;
    }

    fcntl(dsp, F_SETFL, 0);

    if(strstr(as->codec, "pcm-s16le")){
	ofmt = AFMT_S16_LE;
	format = "s16le";
	ssize = 2;
    } else if(strstr(as->codec, "pcm-u16le")){
	ofmt = AFMT_U16_LE;
	format = "u16le";
	ssize = 2;
    } else if(strstr(as->codec, "pcm-s16be")){
	ofmt = AFMT_S16_BE;
	format = "s16be";
	ssize = 2;
    } else if(strstr(as->codec, "pcm-u16be")){
	ofmt = AFMT_U16_BE;
	format = "u16be";
	ssize = 2;
    } else if(strstr(as->codec, "pcm-u8")){
	ofmt = AFMT_U8;
	format = "u8";
	ssize = 1;
    } else {
	tc2_print("OSS", TC2_PRINT_ERROR, "unsupported format %s\n", as->codec);
	goto err;
    }


    if(ioctl(dsp, SNDCTL_DSP_SETFMT, &ofmt) == -1){
	perror("ioctl");
	goto err;
    }

    if(ioctl(dsp, SNDCTL_DSP_CHANNELS, &channels) == -1){
	perror("ioctl");
	goto err;
    }

    if(ioctl(dsp, SNDCTL_DSP_SPEED, &rate) == -1){
	perror("ioctl");
	goto err;
    }

    if(channels != as->channels){
	tc2_print("OSS", TC2_PRINT_WARNING,
		  "%i channels not supported.\n", as->channels);
    }

    if(rate != as->sample_rate){
	tc2_print("OSS", TC2_PRINT_WARNING, "%i Hz sample rate not supported.\n",
		as->sample_rate);
    }

    ioctl(dsp, SNDCTL_DSP_GETOSPACE, &abi);
    tc2_print("OSS", TC2_PRINT_VERBOSE, "%i fragments of %i bytes\n",
	    abi.fragstotal, abi.fragsize);

    oo = calloc(1, sizeof(*oo));
    oo->fd = dsp;
    oo->ssize = channels * ssize;
    oo->format = ofmt;
    oo->channels = channels;
    oo->rate = rate;
    oo->device = device;

    ad = tcallocdz(sizeof(*ad), NULL, oss_free);
    ad->format = format;
    ad->write = oss_write;
    ad->wait = oss_wait;
    ad->delay = oss_delay;
    ad->start = oss_start;
    ad->stop = oss_stop;
    ad->flush = oss_flush;
    ad->private = oo;

    return ad;
err:
    close(dsp);
    return NULL;
}

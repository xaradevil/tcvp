/**
    Copyright (C) 2004  Michael Ahlberg, Måns Rullgård

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

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <tcalloc.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/soundcard.h>
#include <ossrec_tc2.h>

typedef struct oss_in {
    int pcm;
    int bpf;
    u_char *header;
} oss_in_t;

extern int
oss_read(void *buf, size_t size, size_t count, url_t *u)
{
    oss_in_t *oss = u->private;
    size_t bytes = size * count;
    size_t frames;
    ssize_t r;

    if(tcvp_input_oss_conf_timestamp){
	uint64_t pts;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	pts = (uint64_t) tv.tv_sec * 27000000LL + tv.tv_usec * 27;
	memcpy(buf, &pts, sizeof(pts));
	buf += sizeof(pts);
	bytes -= sizeof(pts);
    }

    frames = bytes / oss->bpf;
    bytes = frames * oss->bpf;

    r = read(oss->pcm, buf, bytes);
    if(r < 0){
	tc2_print("OSS", TC2_PRINT_ERROR, "%s\n", strerror(errno));
	return -1;
    }

    return ((tcvp_input_oss_conf_timestamp? 8: 0) + r) / size;
}

extern uint64_t
oss_tell(url_t *u)
{
    return 0;
}

extern int
oss_close(url_t *u)
{
    tcfree(u);
    return 0;
}

static void
oss_free(void *p)
{
    url_t *u = p;
    oss_in_t *oss = u->private;

    close(oss->pcm);
    free(oss->header);
    free(oss);
}

extern url_t *
oss_open(char *name, char *mode)
{
    oss_in_t *oss;
    int pcm;
    u_int rate = url_pcm_conf_rate, channels = url_pcm_conf_channels;
    int hsize, bpf;
    stream_t s;
    char *dev;
    url_t *u, *vu;
    u_int tmp;

    if(*mode != 'r')
	return NULL;

    dev = strchr(name, ':');
    if(!dev || !*++dev)
	dev = tcvp_input_oss_conf_device;

    if((pcm = open(dev, O_RDONLY)) < 0){
	tc2_print("OSS", TC2_PRINT_ERROR, "Can't open '%s' for capture\n",
		  dev);
	return NULL;
    }

    tmp = AFMT_S16_LE;
    if(ioctl(pcm, SNDCTL_DSP_SETFMT, &tmp) == -1){
	tc2_print("OSS", TC2_PRINT_ERROR, "SNDCTL_DSP_SETFMT: %s\n",
		  strerror(errno));
	goto err;
    }

    tmp = channels;
    if(ioctl(pcm, SNDCTL_DSP_CHANNELS, &tmp) == -1){
	tc2_print("OSS", TC2_PRINT_ERROR, "SNDCTL_DSP_CHANNELS: %s\n",
		  strerror(errno));
	goto err;
    }

    if(tmp != channels)
	tc2_print("OSS", TC2_PRINT_WARNING,
		  "%i channels not supported.\n", channels);
    channels = tmp;

    tmp = rate;
    if(ioctl(pcm, SNDCTL_DSP_SPEED, &rate) == -1){
	tc2_print("OSS", TC2_PRINT_ERROR, "SNDCTL_DSP_SPEED: %s\n",
		  strerror(errno));
	goto err;
    }

    if(tmp != rate)
	tc2_print("OSS", TC2_PRINT_WARNING,
		  "%i Hz sample rate not supported.\n", rate);
    rate = tmp;

    bpf = channels * 2;

    memset(&s, 0, sizeof(s));
    s.stream_type = STREAM_TYPE_AUDIO;
    s.audio.codec = "audio/pcm-s16le";
    s.audio.bit_rate = 16 * channels * rate;
    s.audio.sample_rate = rate;
    s.audio.channels = channels;
    s.audio.samples = 0;
    s.audio.block_align = bpf;
    s.audio.sample_size = 16;

    oss = calloc(1, sizeof(*oss));
    oss->pcm = pcm;
    oss->bpf = bpf;
    oss->header = mux_wav_header(&s, &hsize);

    u = tcallocdz(sizeof(*u), NULL, oss_free);
    u->read = oss_read;
    u->tell = oss_tell;
    u->close = oss_close;
    u->private = oss;
    u->size = 0;

    vu = url_vheader_new(u, oss->header, hsize);
    if(tcvp_input_oss_conf_timestamp)
	tcattr_set(vu, "tcvp/timestamp", "tcvp/timestamp", NULL, NULL);
    return vu;

err:
    close(pcm);
    return NULL;
}

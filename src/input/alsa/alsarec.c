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

#include <stdlib.h>
#include <string.h>
#include <tcalloc.h>
#include <sys/time.h>
#include <alsa/asoundlib.h>
#include <alsarec_tc2.h>

typedef struct alsa_in {
    snd_pcm_t *pcm;
    int bpf;
    u_char *header;
} alsa_in_t;

extern int
alsa_read(void *buf, size_t size, size_t count, url_t *u)
{
    alsa_in_t *ai = u->private;
    size_t bytes = size * count;
    snd_pcm_uframes_t frames;

    if(tcvp_input_alsa_conf_timestamp){
	uint64_t pts;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	pts = (uint64_t) tv.tv_sec * 27000000LL + tv.tv_usec * 27;
	memcpy(buf, &pts, sizeof(pts));
	buf += sizeof(pts);
	bytes -= sizeof(pts);
    }

    frames = bytes / ai->bpf;

    while(frames > 0){
	snd_pcm_sframes_t r = snd_pcm_readi(ai->pcm, buf, frames);
	if(r == -EAGAIN || (r >= 0 && r < frames)){
	    snd_pcm_wait(ai->pcm, 100);
	} else if(r == -EPIPE){
	    snd_pcm_prepare(ai->pcm);
	} else if(r < 0){
	    tc2_print("ALSA", TC2_PRINT_ERROR, "%s\n", snd_strerror(r));
	    return -1;
	}

	if(r > 0){
	    frames -= r;
	    buf += r * ai->bpf;
	}
    }

    return count;
}

extern uint64_t
alsa_tell(url_t *u)
{
    return 0;
}

extern int
alsa_close(url_t *u)
{
    tcfree(u);
    return 0;
}

static void
alsa_free(void *p)
{
    url_t *u = p;
    alsa_in_t *ai = u->private;

    snd_pcm_close(ai->pcm);
    free(ai->header);
    free(ai);
}

extern url_t *
alsa_open(char *name, char *mode)
{
    alsa_in_t *ai;
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *hwp;
    u_int rate = url_pcm_conf_rate, channels = url_pcm_conf_channels;
    int hsize, bpf;
    stream_t s;
    char *dev;
    url_t *u, *vu;
    int tmp;

    if(*mode != 'r')
	return NULL;

    dev = strchr(name, ':');
    if(!dev || !*++dev)
	dev = tcvp_input_alsa_conf_device;

    if(snd_pcm_open(&pcm, dev, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)){
	tc2_print("ALSA", TC2_PRINT_ERROR, "Can't open '%s' for capture\n", dev);
	return NULL;
    }

    snd_pcm_hw_params_alloca(&hwp);
    snd_pcm_hw_params_any(pcm, hwp);

    snd_pcm_hw_params_set_access(pcm, hwp, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hwp, SND_PCM_FORMAT_S16_LE);
    tmp = 0;
    snd_pcm_hw_params_set_rate_near(pcm, hwp, &rate, &tmp);
    snd_pcm_hw_params_set_channels_near(pcm, hwp, &channels);

    snd_pcm_hw_params_set_period_size(pcm, hwp,
				      tcvp_input_alsa_conf_period, -1);
    snd_pcm_hw_params_set_buffer_size(pcm, hwp, tcvp_input_alsa_conf_buffer);

    if(snd_pcm_hw_params(pcm, hwp) < 0){
	tc2_print("ALSA", TC2_PRINT_ERROR, "failed setting capture parameters\n");
	goto err;
    }

    snd_pcm_nonblock(pcm, 0);
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

    ai = calloc(1, sizeof(*ai));
    ai->pcm = pcm;
    ai->bpf = bpf;
    ai->header = mux_wav_header(&s, &hsize);

    u = tcallocdz(sizeof(*u), NULL, alsa_free);
    u->read = alsa_read;
    u->tell = alsa_tell;
    u->close = alsa_close;
    u->private = ai;
    u->size = 0;

    vu = url_vheader_new(u, ai->header, hsize);
    if(tcvp_input_alsa_conf_timestamp)
	tcattr_set(vu, "tcvp/timestamp", "tcvp/timestamp", NULL, NULL);
    return vu;

err:
    snd_pcm_close(pcm);
    return NULL;
}

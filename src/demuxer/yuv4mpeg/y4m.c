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
#include <tcmath.h>
#include <yuv4mpeg_tc2.h>

typedef struct yuv4mpeg {
    url_t *url;
    uint64_t pts, ptsd;
    int framesize;
    int offsets[3];
    stream_t s;
    int used;
} yuv4mpeg_t;

typedef struct yuv4mpeg_packet {
    packet_t pk;
    u_char *data[3];
    int size[3];
} yuv4mpeg_packet_t;

static int
read_int(char *p)
{
    int v;

    v = strtol(p, &p, 10);
    if(*p != ' ')
	v = -1;
    return v;
}

static int
read_frac(char *p, tcfraction_t *f)
{
    int n, d;;

    n = strtol(p, &p, 10);
    if(*p != ':')
	return -1;

    p++;
    d = strtol(p, &p, 10);
    if(*p != ' ' && *p != '\n')
	return -1;

    f->num = n;
    f->den = d;
    return 0;
}

static void
y4m_free_pk(void *p)
{
    yuv4mpeg_packet_t *yp = p;
    free(yp->data[0]);
}

extern packet_t *
y4m_packet(muxed_stream_t *ms, int s)
{
    yuv4mpeg_t *y4m = ms->private;
    yuv4mpeg_packet_t *yp;
    char buf[256];
    char *tag = buf;
    uint64_t pts = y4m->pts;

    if(!url_gets(buf, sizeof(buf), y4m->url))
	return NULL;

    if(strncmp(buf, "FRAME", 5))
	return NULL;

    while((tag = strchr(tag, ' '))){
	tag++;
	if(!strncmp(tag, "PTS=", 4)){
	    pts = strtoull(tag + 4, &tag, 10);
	}
    }

    yp = tcallocdz(sizeof(*yp), NULL, y4m_free_pk);
    yp->data[0] = malloc(y4m->framesize);
    yp->data[1] = yp->data[0] + y4m->offsets[1];
    yp->data[2] = yp->data[0] + y4m->offsets[2];
    yp->size[0] = y4m->s.video.width;
    yp->size[1] = y4m->s.video.width / 2;
    yp->size[2] = yp->size[1];
    y4m->url->read(yp->data[0], 1, y4m->framesize, y4m->url);
    yp->pk.data = yp->data;
    yp->pk.sizes = yp->size;
    yp->pk.flags = TCVP_PKT_FLAG_PTS | TCVP_PKT_FLAG_KEY;
    yp->pk.pts = pts;

    y4m->pts = pts + y4m->ptsd;

    return &yp->pk;
}

static void
y4m_free(void *p)
{
    muxed_stream_t *ms = p;
    yuv4mpeg_t *y4m = ms->private;
    tcfree(y4m->url);
}

extern muxed_stream_t *
y4m_open(char *name, url_t *u, tcconf_section_t *conf, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;
    yuv4mpeg_t *y4m;
    int width = 0, height = 0;
    tcfraction_t rate = {0, 0}, aspect = {0, 0};
    char buf[256];
    char *p = buf;

    if(!url_gets(buf, sizeof(buf), u))
	return NULL;

    if(strncmp(buf, "YUV4MPEG2 ", 10)){
	fprintf(stderr, "YUV4MPEG: bad signature\n");
	return NULL;
    }

    while((p = strchr(p, ' '))){
	switch(*++p){
	case 'W':
	    if((width = read_int(++p)) < 0)
		return NULL;
	    break;
	case 'H':
	    if((height = read_int(++p)) < 0)
		return NULL;
	    break;
	case 'F':
	    if(read_frac(++p, &rate))
		return NULL;
	    break;
	case 'A':
	    if(read_frac(++p, &aspect))
		return NULL;
	    break;
	case 'I':
	    break;
	case 'X':
	    break;
	default:
	    return NULL;
	}
    }

    y4m = calloc(1, sizeof(*y4m));
    y4m->url = tcref(u);
    y4m->framesize = width * height * 3 / 2;
    y4m->offsets[1] = width * height;
    y4m->offsets[2] = y4m->offsets[1] + width * height / 4;
    y4m->s.stream_type = STREAM_TYPE_VIDEO;
    y4m->s.video.codec = "video/raw-i420";
    y4m->s.video.bit_rate = y4m->framesize * 8 * rate.num / rate.den;
    y4m->s.video.frame_rate = rate;
    y4m->s.video.width = width;
    y4m->s.video.height = height;
    y4m->s.video.aspect.num = width * aspect.num;
    y4m->s.video.aspect.den = height * aspect.den;
    tcreduce(&y4m->s.video.aspect);
    y4m->ptsd = 27000000LL * rate.den / rate.num;

    ms = tcallocdz(sizeof(*ms), NULL, y4m_free);
    ms->n_streams = 1;
    ms->streams = &y4m->s;
    ms->used_streams = &y4m->used;
    ms->next_packet = y4m_packet;
    ms->private = y4m;

    return ms;
}

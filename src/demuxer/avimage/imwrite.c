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
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <ffmpeg/avformat.h>
#include <tcvp_types.h>
#include <avimage_tc2.h>

typedef struct image_write {
    char *file;
    int pixfmt;
    uint64_t pts;
    uint32_t interval;
    int frame;
    char *tag;
    int probed;
} image_write_t;

static int
im_input(tcvp_pipe_t *p, packet_t *pk)
{
    image_write_t *iw = p->private;
    AVImageInfo aii;
    ByteIOContext bio;
    char file[strlen(iw->file) + 12];
    int i;

    if(!pk->data)
	goto end;
    if(pk->pts < iw->pts)
	goto end;

    sprintf(file, iw->file, iw->frame);
    if(url_fopen(&bio, file, URL_WRONLY)){
	goto end;
    }

    aii.pix_fmt = iw->pixfmt;
    aii.width = p->format.video.width;
    aii.height = p->format.video.height;
    aii.interleaved = 0;
    for(i = 0; i < 3; i++){
	aii.pict.data[i] = pk->data[i];
	aii.pict.linesize[i] = pk->sizes[i];
    }

    av_write_image(&bio, &jpeg_image_format, &aii);
    url_fclose(&bio);

    iw->pts = pk->pts + iw->interval;
    iw->frame++;

end:
    tcfree(pk);
    return 0;
}

static int
im_flush(tcvp_pipe_t *p, int drop)
{
    return 0;
}

static struct {
    char *name;
    enum PixelFormat pxf;
} codecs[] = {
    { "video/raw-i420", PIX_FMT_YUVJ420P },
    { NULL, 0 }
};

static int
im_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    image_write_t *iw = p->private;
    int i;

    if(iw->probed)
	return PROBE_FAIL;

    for(i = 0; codecs[i].name; i++){
	if(!strcmp(s->common.codec, codecs[i].name))
	    break;
    }

    if(!codecs[i].name){
	tc2_print("AVIMAGE", TC2_PRINT_ERROR,
		  "Unsupported format %s\n", s->common.codec);
	return PROBE_FAIL;
    }

    iw->pixfmt = codecs[i].pxf;
    iw->probed = 1;

    p->format = *s;
    p->format.stream_type = STREAM_TYPE_MULTIPLEX;
    p->format.common.codec = iw->tag;
    p->format.common.bit_rate = 0;
    return PROBE_OK;
}

static void
im_free(void *p)
{
    tcvp_pipe_t *tp = p;
    image_write_t *iw = tp->private;
    free(iw);
}

extern tcvp_pipe_t *
im_new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
       muxed_stream_t *ms)
{
    tcvp_pipe_t *p;
    image_write_t *iw;
    char *url;

    if(tcconf_getvalue(cs, "mux/url", "%s", &url) <= 0){
	tc2_print("AVIMAGE", TC2_PRINT_ERROR, "No output file\n");
	return NULL;
    }

    if(strncmp(s->common.codec, "video/raw", 9)){
	tc2_print("AVIMAGE", TC2_PRINT_ERROR,
		  "Unsupported format %s\n", s->common.codec);
	return NULL;
    }

    iw = calloc(1, sizeof(*iw));
    iw->file = url;
    iw->tag = "jpeg";
    tcconf_getvalue(cs, "interval", "%i", &iw->interval);
    iw->interval *= 27000;

    p = tcallocdz(sizeof(*p), NULL, im_free);
    p->input = im_input;
    p->flush = im_flush;
    p->probe = im_probe;
    p->private = iw;

    return p;
}

extern int
im_init(char *p)
{
    av_register_all();
    return 0;
}

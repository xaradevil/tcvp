/**
    Copyright (C) 2003, 2004  Michael Ahlberg, Måns Rullgård

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

#define _LINUX_TIME_H 1
#define _DEVICE_H_ 1

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <tcalloc.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <linux/videodev.h>
#include <v4l_tc2.h>

typedef struct v4l {
    int dev;
    int framesize, fhsize;
    int nbufs;
    struct {
        struct v4l2_buffer v4lb;
        char *buf;
    } *buffers, buf;
    char *fhead, *frame;
    int pos;
    int (*get_buffer)(struct v4l *);
    char *head;
} v4l_t;

#define min(a, b) ((a)<(b)? (a): (b))

#define nbuffers tcvp_input_v4l_conf_buffers

static struct {
    int cap;
    char *desc;
} v4l_caps[] = {
    { V4L2_CAP_VIDEO_CAPTURE, "video capture" },
    { V4L2_CAP_VIDEO_OUTPUT,  "video output" },
    { V4L2_CAP_VIDEO_OVERLAY, "video overlay" },
    { V4L2_CAP_VBI_CAPTURE,   "VBI capture" },
    { V4L2_CAP_VBI_OUTPUT,    "VBI output" },
    { V4L2_CAP_RDS_CAPTURE,   "RDS capture" },
    { V4L2_CAP_TUNER,         "tuner" },
    { V4L2_CAP_AUDIO,         "audio" },
    { V4L2_CAP_READWRITE,     "read/write I/O" },
    { V4L2_CAP_ASYNCIO,       "async I/O" },
    { V4L2_CAP_STREAMING,     "streaming I/O" }
};

static char *input_types[] = {
    [V4L2_INPUT_TYPE_TUNER] = "tuner",
    [V4L2_INPUT_TYPE_CAMERA] = "camera"
};

#define NCAPS (sizeof(v4l_caps)/sizeof(v4l_caps[0]))

static int
v4l_get_buffer_up(v4l_t *v4l)
{
    tc2_print("V4L2", TC2_PRINT_VERBOSE, "get_buffer_up\n");

    if(v4l->frame)
        ioctl(v4l->dev, VIDIOC_QBUF, &v4l->buf.v4lb);
    if(ioctl(v4l->dev, VIDIOC_DQBUF, &v4l->buf.v4lb)){
        tc2_print("V4L2", TC2_PRINT_ERROR, "VIDIOC_DQBUF: %s\n",
                  strerror(errno));
        return -1;
    }

    v4l->frame = (char *) v4l->buf.v4lb.m.userptr;

    return 0;
}

static int
v4l_get_buffer_mmap(v4l_t *v4l)
{
    uint64_t pts;

    tc2_print("V4L2", TC2_PRINT_DEBUG, "get_buffer_mmap\n");

    if(v4l->frame){
        if(ioctl(v4l->dev, VIDIOC_QBUF, &v4l->buf.v4lb)){
            tc2_print("V4L2", TC2_PRINT_ERROR, "VIDIOC_QBUF: %s\n",
                      strerror(errno));
            return -1;
        }
    }

    if(ioctl(v4l->dev, VIDIOC_DQBUF, &v4l->buf.v4lb)){
        tc2_print("V4L2", TC2_PRINT_ERROR, "VIDIOC_DQBUF: %s\n",
                  strerror(errno));
        return -1;
    }

    v4l->frame = v4l->buffers[v4l->buf.v4lb.index].buf;
    pts = (uint64_t) v4l->buf.v4lb.timestamp.tv_sec * 27000000LL +
        v4l->buf.v4lb.timestamp.tv_usec * 27;
    v4l->fhsize = sprintf(v4l->fhead, "FRAME Xpts=%llu\n", pts);
    tc2_print("V4L2", TC2_PRINT_VERBOSE+1, "pts = %lli\n", pts / 27);

    return 0;
}

static int
v4l_get_buffer_read(v4l_t *v4l)
{
    if(read(v4l->dev, v4l->frame, v4l->framesize) < 0){
        tc2_print("V4L2", TC2_PRINT_ERROR, "read error: %s\n",
                  strerror(errno));
        return -1;
    }

    return 0;
}

static int
v4l_read(void *buf, size_t size, size_t count, url_t *u)
{
    v4l_t *v4l = u->private;
    size_t bytes;
    char *src;

    if(v4l->pos >= v4l->framesize + v4l->fhsize || !v4l->frame){
        if(v4l->get_buffer(v4l))
            return -1;
        v4l->pos = 0;
    }

    bytes = size * count;

    if(v4l->pos < v4l->fhsize){
        bytes = min(bytes, v4l->fhsize - v4l->pos);
        src = v4l->fhead + v4l->pos;
    } else {
        bytes = min(bytes, v4l->framesize - v4l->pos + v4l->fhsize);
        src = v4l->frame + v4l->pos - v4l->fhsize;
    }

    memcpy(buf, src, bytes);
    v4l->pos += bytes;

    return bytes / size;
}

static void
v4l_free(void *p)
{
    url_t *u = p;
    v4l_t *v4l = u->private;

    free(v4l->head);
    close(v4l->dev);
    free(v4l);
}

static int
v4l_close(url_t *u)
{
    tcfree(u);
    return 0;
}

extern url_t *
v4l_open(char *name, char *mode)
{
    struct v4l2_capability cap;
    struct v4l2_input input;
    struct v4l2_format format;
    struct v4l2_fmtdesc fmtdesc;
    struct v4l2_standard std, sstd;
    struct v4l2_requestbuffers rqb;
    v4l2_std_id stdid;
    v4l_t *v4l;
    url_t *u;
    int fd, i;
    char *dev;
    int hsize;

    dev = strchr(name, ':');
    if(!dev || !*++dev)
        dev = url_video_conf_device;

    if((fd = open(dev, O_RDWR)) < 0)
        return NULL;

    if(ioctl(fd, VIDIOC_QUERYCAP, &cap)){
        tc2_print("V4L2", TC2_PRINT_ERROR, "not a v4l2 device\n");
        goto err;
    }

    tc2_print("V4L2", TC2_PRINT_VERBOSE, "card %s\n", cap.card);
    for(i = 0; i < NCAPS; i++)
        if(cap.capabilities & v4l_caps[i].cap)
            tc2_print("V4L2", TC2_PRINT_VERBOSE, "  supports %s\n",
                      v4l_caps[i].desc);

    if(ioctl(fd, VIDIOC_G_INPUT, &i)){
        tc2_print("V4L2", TC2_PRINT_ERROR, "can't get current input.\n");
        goto err;
    }

    tc2_print("V4L2", TC2_PRINT_VERBOSE, "current input %i\n", i);

    for(i = 0;; i++){
        input.index = i;
        if(ioctl(fd, VIDIOC_ENUMINPUT, &input))
            break;
        tc2_print("V4L2", TC2_PRINT_VERBOSE, "input %i '%s'\n",
                  i, input.name);
        tc2_print("V4L2", TC2_PRINT_VERBOSE, "  type %s\n",
                  input_types[input.type]);
        tc2_print("V4L2", TC2_PRINT_VERBOSE, "  audioset %x\n",
                  input.audioset);
        if(input.type == V4L2_INPUT_TYPE_TUNER)
            tc2_print("V4L2", TC2_PRINT_VERBOSE, "  tuner %i\n",
                      input.tuner);
    }

    tc2_print("V4L2", TC2_PRINT_VERBOSE, "supported formats:\n");
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for(i = 0;; i++){
        fmtdesc.index = i;
        if(ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc))
            break;
        tc2_print("V4L2", TC2_PRINT_VERBOSE, "  %x: %s\n",
                  fmtdesc.pixelformat, fmtdesc.description);
    }

    memset(&sstd, 0, sizeof(sstd));
    tc2_print("V4L2", TC2_PRINT_VERBOSE, "supported standards:\n");
    for(i = 0;; i++){
        std.index = i;
        if(ioctl(fd, VIDIOC_ENUMSTD, &std))
            break;
        if(!strcmp(std.name, tcvp_input_v4l_conf_standard))
            sstd = std;
        tc2_print("V4L2", TC2_PRINT_VERBOSE, "  name '%s'\n", std.name);
        tc2_print("V4L2", TC2_PRINT_VERBOSE, "    id %llx\n", std.id);
        tc2_print("V4L2", TC2_PRINT_VERBOSE, "    period %i / %i\n",
                  std.frameperiod.numerator, std.frameperiod.denominator);
        tc2_print("V4L2", TC2_PRINT_VERBOSE, "    lines %i\n", std.framelines);
    }

    if(sstd.id)
        ioctl(fd, VIDIOC_S_STD, &sstd.id);
    ioctl(fd, VIDIOC_G_STD, &stdid);
    tc2_print("V4L2", TC2_PRINT_VERBOSE, "using standard %llx\n", stdid);

    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd, VIDIOC_G_FMT, &format)){
        tc2_print("V4L2", TC2_PRINT_ERROR, "can't get format.\n");
        goto err;
    }

    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
    format.fmt.pix.width = tcvp_input_v4l_conf_width;
    format.fmt.pix.height = tcvp_input_v4l_conf_height;
    format.fmt.pix.bytesperline = 0;

    if(ioctl(fd, VIDIOC_S_FMT, &format)){
        tc2_print("V4L2", TC2_PRINT_ERROR, "can't set format: %m\n");
        goto err;
    }

    tc2_print("V4L2", TC2_PRINT_VERBOSE, "width %i\n", format.fmt.pix.width);
    tc2_print("V4L2", TC2_PRINT_VERBOSE, "height %i\n", format.fmt.pix.height);
    tc2_print("V4L2", TC2_PRINT_VERBOSE, "format %x\n",
              format.fmt.pix.pixelformat);
    tc2_print("V4L2", TC2_PRINT_VERBOSE, "bytes/line %i\n",
              format.fmt.pix.bytesperline);
    tc2_print("V4L2", TC2_PRINT_VERBOSE, "image size %i\n",
              format.fmt.pix.sizeimage);

    v4l = calloc(1, sizeof(*v4l));
    v4l->dev = fd;
    v4l->framesize = format.fmt.pix.sizeimage;
    v4l->head = malloc(256);
    hsize = snprintf(v4l->head, 256, "YUV4MPEG2 W%i H%i F%i:%i A%i:%i\n",
                     format.fmt.pix.width, format.fmt.pix.height,
                     sstd.frameperiod.denominator, sstd.frameperiod.numerator,
                     24, 11);
    v4l->fhead = malloc(256);

    rqb.count = nbuffers;
    rqb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    rqb.memory = V4L2_MEMORY_USERPTR;
    if(0 && !ioctl(fd, VIDIOC_REQBUFS, &rqb)){
        int t = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        tc2_print("V4L2", TC2_PRINT_VERBOSE, "using user pointer IO\n");
        v4l->nbufs = nbuffers;
        v4l->buffers = calloc(nbuffers, sizeof(*v4l->buffers));
        for(i = 0; i < nbuffers; i++){
            v4l->buffers[i].buf = malloc(v4l->framesize);
            v4l->buffers[i].v4lb.index = i;
            v4l->buffers[i].v4lb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            v4l->buffers[i].v4lb.memory = V4L2_MEMORY_USERPTR;
            v4l->buffers[i].v4lb.m.userptr =
                (unsigned long) v4l->buffers[i].buf;
            v4l->buffers[i].v4lb.length = v4l->framesize;
            if(ioctl(fd, VIDIOC_QBUF, &v4l->buffers[i].v4lb)){
                tc2_print("V4L2", TC2_PRINT_ERROR,
                          "error enqueuing buffer %i: %s\n",
                          i, strerror(errno));
                goto err;
            }
            tc2_print("V4L2", TC2_PRINT_VERBOSE, "  buffer %i @ %p\n",
                      i, v4l->buffers[i].buf);
        }
        v4l->buf.v4lb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l->get_buffer = v4l_get_buffer_up;
        if(ioctl(fd, VIDIOC_STREAMON, &t)){
            tc2_print("V4L2", TC2_PRINT_ERROR, "VIDIOC_STREAMON: %s\n",
                      strerror(errno));
            goto err;
        }
    } else if(rqb.memory = V4L2_MEMORY_MMAP,
              !ioctl(fd, VIDIOC_REQBUFS, &rqb)){
        int t = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        tc2_print("V4L2", TC2_PRINT_VERBOSE, "using mmap IO, %i buffers\n",
                  rqb.count);
        v4l->nbufs = rqb.count;
        v4l->buffers = calloc(v4l->nbufs, sizeof(*v4l->buffers));
        for(i = 0; i < v4l->nbufs; i++){
            v4l->buffers[i].v4lb.index = i;
            v4l->buffers[i].v4lb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if(ioctl(fd, VIDIOC_QUERYBUF, &v4l->buffers[i].v4lb)){
                tc2_print("V4L2", TC2_PRINT_ERROR,
                          "error querying buffer %i: %s\n",
                          i, strerror(errno));
                goto err;
            }
            v4l->buffers[i].buf = mmap(NULL, v4l->buffers[i].v4lb.length,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, fd,
                                       v4l->buffers[i].v4lb.m.offset);
            tc2_print("V4L2", TC2_PRINT_VERBOSE, "  buffer %i @ %p\n",
                      i, v4l->buffers[i].buf);
            if(ioctl(fd, VIDIOC_QBUF, &v4l->buffers[i].v4lb)){
                tc2_print("V4L2", TC2_PRINT_ERROR,
                          "error enqueuing buffer %i: %s\n",
                          i, strerror(errno));
                goto err;
            }
        }

        v4l->buf.v4lb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l->get_buffer = v4l_get_buffer_mmap;
        if(ioctl(fd, VIDIOC_STREAMON, &t)){
            tc2_print("V4L2", TC2_PRINT_ERROR, "VIDIOC_STREAMON: %s\n",
                      strerror(errno));
            goto err;
        }
    } else {
        tc2_print("V4L2", TC2_PRINT_VERBOSE, "using read/write IO\n");
        v4l->frame = malloc(v4l->framesize);
        v4l->fhsize = sprintf(v4l->fhead, "FRAME\n");
        v4l->get_buffer = v4l_get_buffer_read;
    }

    u = tcallocdz(sizeof(*u), NULL, v4l_free);
    u->read = v4l_read;
    u->close = v4l_close;
    u->flags = URL_FLAG_STREAMED;
    u->private = v4l;

    return url_vheader_new(u, v4l->head, hsize);

err:
    close(fd);
    return NULL;
}

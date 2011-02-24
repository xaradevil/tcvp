/**
    Copyright (C) 2003-2006  Michael Ahlberg, Måns Rullgård

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
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xvlib.h>
#include <pthread.h>
#include <tcvp_types.h>
#include <xv_tc2.h>

#define FRAMES 64

typedef struct xv_window {
    Display *dpy;
    Window win;
    GC gc;
    XvPortID port;
    int width, height;
    int dx, dy, dw, dh;
    XShmSegmentInfo *shm;
    XvImage **images;
    window_manager_t *wm;
    int last_frame;
    pthread_mutex_t flock;
    tcvp_module_t *mod;
} xv_window_t;

static int
do_show(xv_window_t *xvw, int frame)
{
    pthread_mutex_lock(&xvw->flock);

    XvShmPutImage(xvw->dpy, xvw->port, xvw->win, xvw->gc,
                  xvw->images[frame],
                  0, 0, xvw->width, xvw->height,
                  xvw->dx, xvw->dy, xvw->dw, xvw->dh,
                  False);
    XSync(xvw->dpy, False);

    xvw->last_frame = frame;

    pthread_mutex_unlock(&xvw->flock);
    return 0;
}

static int
xv_show(video_driver_t *vd, int frame)
{
    return do_show(vd->private, frame);
}

static int
xv_get(video_driver_t *vd, int frame, u_char **data, int *strides)
{
    xv_window_t *xvw = vd->private;
    XvImage *xi = xvw->images[frame];
    int i;

    pthread_mutex_lock(&xvw->flock);
    if(frame == xvw->last_frame){
        if(xvw->last_frame == vd->frames - 1)
            xvw->last_frame = 0;
        else
            xvw->last_frame++;
    }
    pthread_mutex_unlock(&xvw->flock);

    for(i = 0; i < xi->num_planes; i++){
        data[i] = (u_char*)xi->data + xi->offsets[i];
        strides[i] = xi->pitches[i];
    }

    return xi->num_planes;
}

extern int
xve_show(tcvp_module_t *m, tcvp_event_t *te)
{
    xv_window_t *xvw = m->private;
    if(xvw->last_frame > -1)
        do_show(xvw, xvw->last_frame);
    return 0;
}

extern int
xve_move(tcvp_module_t *m, tcvp_event_t *te)
{
    xv_window_t *xvw = m->private;
    tcvp_wm_move_event_t *me = (tcvp_wm_move_event_t *) te;

    xvw->dx = me->x;
    xvw->dy = me->y;
    xvw->dw = me->w;
    xvw->dh = me->h;

    if(xvw->last_frame > -1)
        do_show(xvw, xvw->last_frame);

    return 0;
}

static int
xv_close(video_driver_t *vd)
{
    xv_window_t *xvw = vd->private;
    int i;

    xvw->mod->private = NULL;
    tcfree(xvw->mod);

    for(i = 0; i < vd->frames; i++){
        XShmDetach(xvw->dpy, &xvw->shm[i]);
        shmdt(xvw->shm[i].shmaddr);
        XFree(xvw->images[i]);
    }

    xvw->wm->close(xvw->wm);
    XCloseDisplay(xvw->dpy);

    pthread_mutex_destroy(&xvw->flock);
    free(xvw->images);
    free(xvw->shm);
    free(xvw);
    free(vd);

    return 0;
}

static struct {
    char *name;
    uint32_t tag;
} formats[] = {
    { "i420", 0x30323449 },
    { "yv12", 0x32315659 },
    { "yuy2", 0x32595559 },
    { "yvyu", 0x55595659 },
    { "uyvy", 0x59565955 },
    { "rgb555", 0x35315652 },
    { "rgb565", 0x36315652 }
};

#define NUMFORMATS (sizeof(formats) / sizeof(formats[0]))

static uint32_t
xv_get_format(const char *fmt)
{
    unsigned int i;

    for(i = 0; i < NUMFORMATS; i++){
        if(!strcmp(fmt, formats[i].name)){
            return formats[i].tag;
        }
    }

    return 0;
}

static int
xv_valid_attr(XvAttribute *attr, int n, char *name, int set, int value)
{
    int i;

    for(i = 0; i < n; i++){
        if(!strcmp(attr[i].name, name))
            break;
    }

    if(i == n)
        return -1;

    if(!set)
        return attr[i].flags & XvGettable? 0: -1;

    if(!(attr[i].flags & XvSettable))
        return -1;

    if(value < attr[i].min_value || value > attr[i].max_value)
        return -1;

    return 0;
}

extern video_driver_t *
xv_open(video_stream_t *vs, tcconf_section_t *cs)
{
    video_driver_t *vd;
    xv_window_t *xvw;
    Display *dpy;
    unsigned int ver, rev, rb, evb, erb;
    XvAdaptorInfo *xai;
    XvAttribute *xvattr;
    u_int na;
    int nattr;
    Window win;
    int i;
    int frames = tcvp_driver_video_xv_conf_frames?: FRAMES;
    int color_key;
    Atom atm;
    window_manager_t *wm;
    char *display = NULL;
    float dasp = 0;
    char *fmt;
    uint32_t fmtid = 0;
    int port = 0;

    fmt = strstr(vs->codec, "raw-");
    if(!fmt)
        return NULL;
    fmt += 4;

    fmtid = xv_get_format(fmt);

    if(!fmtid && tcvp_driver_video_xv_conf_format){
        fmt = tcvp_driver_video_xv_conf_format;
        tc2_print("XV", TC2_PRINT_WARNING, "using default format '%s'\n", fmt);
        fmtid = xv_get_format(fmt);
    }

    if(!fmtid){
        tc2_print("XV", TC2_PRINT_ERROR, "unknown format '%s'\n", fmt);
        return NULL;
    }

    if(cs){
        tcconf_getvalue(cs, "video/device", "%s", &display);
        tcconf_getvalue(cs, "video/aspect", "%f", &dasp);
    }

    XInitThreads();

    if((dpy = XOpenDisplay(display)) == NULL){
        tc2_print("XV", TC2_PRINT_ERROR, "can't open display %s\n",
                  display? display: "NULL");
        return NULL;
    }

    if(XvQueryExtension(dpy, &ver, &rev, &rb, &evb, &erb) != Success){
        tc2_print("XV", TC2_PRINT_ERROR, "XVideo extension not present\n");
        return NULL;
    }

    tc2_print("XV", TC2_PRINT_DEBUG, "XVideo version %u.%u\n", ver, rev);

    if(XvQueryAdaptors(dpy, DefaultRootWindow(dpy), &na, &xai) != Success){
        tc2_print("XV", TC2_PRINT_ERROR, "XvQueryAdaptors failed\n");
        return NULL;
    }

    tc2_print("XV", TC2_PRINT_DEBUG, "%i adaptors\n", na);

    for(i = 0; i < na && !port; i++){
        int j;

        tc2_print("XV", TC2_PRINT_DEBUG,
                  "Adaptor #%i: \"%s\", %i ports, base %i\n",
                  i, xai[i].name, xai[i].num_ports, xai[i].base_id);

        for(j = 0; j < xai[i].num_ports && !port; j++){
            XvImageFormatValues *xif;
            int nf, k;

            xif = XvListImageFormats(dpy, xai[i].base_id + j, &nf);
            for(k = 0; k < nf; k++){
                if(xif[k].id == fmtid){
                    port = xai[i].base_id + j;
                    break;
                }
            }
            XFree(xif);
        }
    }

    XvFreeAdaptorInfo(xai);

    if(!port){
        tc2_print("XV", TC2_PRINT_ERROR,
                  "no suitable port for format %s\n", fmt);
        return NULL;
    }

    tc2_print("XV", TC2_PRINT_DEBUG, "using port %i\n", port);

    xvw = calloc(1, sizeof(*xvw));
    xvw->port = port;
    xvw->width = vs->width;
    xvw->height = vs->height;
    xvw->dw = vs->width;
    xvw->dh = vs->height;
    xvw->shm = malloc(frames * sizeof(*xvw->shm));
    xvw->images = malloc(frames * sizeof(*xvw->images));
    xvw->last_frame = -1;
    pthread_mutex_init(&xvw->flock, NULL);

    if(dasp > 0 || (vs->aspect.num && vs->aspect.den)){
        float asp = (float) vs->width / vs->height;
        if(dasp <= 0)
            dasp = (float) vs->aspect.num / vs->aspect.den;
        if(dasp > asp)
            xvw->dw = (float) vs->height * dasp;
        else
            xvw->dh = (float) vs->width / dasp;
    }

    xvattr = XvQueryPortAttributes(dpy, port, &nattr);

    for(i = 0; i < driver_video_xv_conf_attribute_count; i++){
        char *name = driver_video_xv_conf_attribute[i].name;
        int value = driver_video_xv_conf_attribute[i].value;
        if(!xv_valid_attr(xvattr, nattr, name, 1, value)){
            if((atm = XInternAtom(dpy, name, True)) != None){
                XvSetPortAttribute(dpy, xvw->port, atm, value);
            }
        } else {
            tc2_print("XV", TC2_PRINT_WARNING,
                      "can't set attribute %s to %i\n", name, value);
        }
    }

    if((atm = XInternAtom(dpy, "XV_COLORKEY", True)) != None){
        if(tcconf_getvalue(cs, "video/color_key", "%i", &color_key) > 0){
            if(!xv_valid_attr(xvattr, nattr, "XV_COLORKEY", 1, color_key))
                XvSetPortAttribute(dpy, xvw->port, atm, color_key);
            else
                tc2_print("XV", TC2_PRINT_WARNING, "can't set color key\n");
        } else {
            if(!xv_valid_attr(xvattr, nattr, "XV_COLORKEY", 0, 0)){
                XvGetPortAttribute(dpy, xvw->port, atm, &color_key);
                tcconf_clearvalue(cs, "video/color_key");
                tcconf_setvalue(cs, "video/color_key", "%i", color_key);
            }
        }
    }

    XFree(xvattr);

    xvw->dpy = dpy;
    xvw->mod = driver_video_xv_new(cs);
    xvw->mod->private = xvw;
    xvw->mod->init(xvw->mod);

    if(!(wm = wm_x11_open(xvw->dw, xvw->dh, cs, 0))){
        tcfree(xvw->mod);
        free(xvw);
        return NULL;
    }
    wm_x11_getwindow(wm, &dpy, &win);

    xvw->win = win;
    xvw->gc = DefaultGC(xvw->dpy, DefaultScreen(xvw->dpy));
    xvw->wm = wm;

    for(i = 0; i < frames; i++){
        XvImage *xvi;
        XShmSegmentInfo *shm = &xvw->shm[i];

        xvi = XvShmCreateImage(xvw->dpy, xvw->port, fmtid, NULL,
                               vs->width, vs->height, shm);
        shm->shmid = shmget(IPC_PRIVATE, xvi->data_size, IPC_CREAT | 0777);
        shm->shmaddr = shmat(shm->shmid, 0, 0);
        shm->readOnly = False;
        xvi->data = shm->shmaddr;
        XShmAttach(xvw->dpy, shm);
        shmctl(shm->shmid, IPC_RMID, NULL); /* delete now in case we crash */

        xvw->images[i] = xvi;
    }

    XSync(xvw->dpy, False);

    vd = calloc(1, sizeof(*vd));
    vd->frames = frames;
    vd->pixel_format = fmt;
    vd->get_frame = xv_get;
    vd->show_frame = xv_show;
    vd->close = xv_close;
    vd->private = xvw;

    if(display)
        free(display);

    return vd;
}

/**
    Copyright (C) 2006  Michael Ahlberg, Måns Rullgård

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
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <postproc/postprocess.h>
#include <tcvp_types.h>
#include <ffpostproc_tc2.h>

typedef struct postproc {
    pp_context_t *ctx;
    pp_mode_t *mode;
} postproc_t;

typedef struct pp_packet {
    tcvp_data_packet_t pk;
    u_char *data[3];
    int sizes[3];
} pp_packet_t;

static void
pp_free_pk(void *p)
{
    pp_packet_t *pk = p;
    free(pk->data[0]);
    free(pk->data[1]);
    free(pk->data[2]);
}

extern int
pp_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    postproc_t *pp = p->private;

    if(pk->data){
        pp_packet_t *npk = tcallocdz(sizeof(*npk), NULL, pp_free_pk);
        int w = p->format.video.width;
        int h = p->format.video.height;

        npk->pk = *pk;
        npk->pk.data = npk->data;
        npk->pk.sizes = npk->sizes;
        npk->pk.private = NULL;

        npk->data[0] = malloc(h * pk->sizes[0]);
        npk->data[1] = malloc(h * pk->sizes[1] / 2);
        npk->data[2] = malloc(h * pk->sizes[2] / 2);

        npk->sizes[0] = pk->sizes[0];
        npk->sizes[1] = pk->sizes[1];
        npk->sizes[2] = pk->sizes[2];

        pp_postprocess(pk->data, pk->sizes, npk->data, npk->sizes,
                       w, h, NULL, 0, pp->mode, pp->ctx, 0);

        tcfree(pk);
        pk = (tcvp_data_packet_t *) npk;
    }

    return p->next->input(p->next, (tcvp_packet_t *) pk);
}

extern int
pp_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    postproc_t *pp = p->private;

    if(strcmp(s->common.codec, "video/raw-yuv420p") &&
       strcmp(s->common.codec, "video/raw-i420")){
        tc2_print("POSTPROC", TC2_PRINT_ERROR, "unsupported pixel format %s\n",
                  s->common.codec);
        return PROBE_FAIL;
    }

    pp->ctx = pp_get_context(p->format.video.width, p->format.video.height,
                             PP_FORMAT_420);
    return PROBE_OK;
}

static void
pp_free(void *p)
{
    postproc_t *pp = p;
    if(pp->mode)
        pp_free_mode(pp->mode);
    if(pp->ctx)
        pp_free_context(pp->ctx);
}

extern int
pp_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
       muxed_stream_t *ms)
{
    postproc_t *pp;
    char *pm = NULL;
    int qual = PP_QUALITY_MAX;

    tcconf_getvalue(cs, "mode", "%s", &pm);
    tcconf_getvalue(cs, "quality", "%i", &qual);

    if(pm == NULL)
        pm = strdup("default");

    pp = tcallocdz(sizeof(*pp), NULL, pp_free);
    pp->mode = pp_get_mode_by_name_and_quality(pm, qual);

    p->private = pp;

    free(pm);
    return 0;
}

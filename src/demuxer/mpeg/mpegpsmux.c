/**
    Copyright (C) 2003-2004  Michael Ahlberg, Måns Rullgård

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
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcendian.h>
#include <tcvp_types.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

struct mpegps_mux {
    url_t *out;
    int bitrate;
    int pessize;
    int nvideo, naudio;
    int astreams, nstreams;
    struct mpegps_output_stream {
        int stream_type;
        int stream_id;
        uint64_t dts;
        stream_t *str;
        int ac3id;
        uint64_t last_pts;
    } *streams;
    int psm, syshdr;
    int vid, aid, ac3id;
    int pts_interval;
};

static int
write_pack_header(u_char *d, uint64_t scr, int rate)
{
    uint64_t scr_base;
    int scr_ext;

    scr_base = (scr / 300) & ((1LL << 33) - 1);
    scr_ext = (scr % 300) & 0x1ff;

    st_unaligned32(htob_32(0x100 | PACK_HEADER), d);
    d += 4;

    *d++ = 0x40 | ((scr_base >> 27) & 0x38) | 0x4 | ((scr_base >> 28) & 0x3);
    *d++ = (scr_base >> 20) & 0xff;
    *d++ = ((scr_base >> 12) & 0xf8) | 0x4 | ((scr_base >> 13) & 0x3);
    *d++ = (scr_base >> 5) & 0xff;
    *d++ = ((scr_base & 0x1f) << 3) | 0x4 | ((scr_ext >> 7) & 0x3);
    *d++ = ((scr_ext << 1) & 0xfe) | 1;

    st_unaligned32(htob_32((rate << 10) | 0x3f8), d);
    d += 4;

    return 14;
}

static int
write_system_header(u_char *d, struct mpegps_mux *psm)
{
    int i, ns = 0;
    u_char *l;

    st_unaligned32(htob_32(0x100 | SYSTEM_HEADER), d);
    d += 4;
    l = d;
    d += 2;

    st_unaligned32(htob_32((1 << 31) | ((psm->bitrate / (8 * 50)) << 9) |
                           0x80 | psm->naudio), d);
    d += 4;

    *d++ = 0x20 | psm->nvideo;
    *d++ = 0x7f;

    for(i = 0; i < psm->nstreams; i++){
        if(psm->streams[i].stream_id){
            int scale, size;
        
            *d++ = psm->streams[i].stream_id;
            if((psm->streams[i].stream_id & 0xf0) == 0xe0){
                scale = 1;
                size = 46 * 8;
            } else {
                scale = 0;
                size = 4 * 8;
            }
            st_unaligned16(htob_16(0xc000 | (scale << 13) | size), d);
            d += 2;
            ns++;
        }
    }

    st_unaligned16(htob_16(6 + ns * 3), l);

    return 12 + ns * 3;
}

static int
write_psm(u_char *d, struct mpegps_mux *psm, int size)
{
    u_char *p = d;
    u_char *ml, *el;
    uint32_t crc;
    int i;

    st_unaligned32(htob_32(0x100 | PROGRAM_STREAM_MAP), d);
    d += 4;
    ml = d;
    d += 2;

    *d++ = 0xe0;
    *d++ = 0xff;
    *d++ = 0;
    *d++ = 0;
    el = d;
    d += 2;

    tc2_print("MPEGPS-MUX", TC2_PRINT_DEBUG,
              "writing PSM for %i streams\n", psm->astreams);

    for(i = 0; i < psm->astreams; i++){
        if(psm->streams[i].stream_id){
            *d++ = psm->streams[i].stream_type;
            if(psm->streams[i].stream_id == PRIVATE_STREAM_1)
                *d++ = psm->streams[i].ac3id;
            else
                *d++ = psm->streams[i].stream_id;

            if(psm->streams[i].str->stream_type == STREAM_TYPE_VIDEO){
                int dl, eil = 0;
                u_char *il = d;

                d += 2;

                dl = write_mpeg_descriptor(psm->streams[i].str,
                                           VIDEO_STREAM_DESCRIPTOR,
                                           d, 1024 - (d - p));
                d += dl;
                eil += dl;
                dl = write_mpeg_descriptor(psm->streams[i].str,
                                           TARGET_BACKGROUND_GRID_DESCRIPTOR,
                                           d, 1024 - (d - p));
                d += dl;
                eil += dl;
                st_unaligned16(htob_16(eil), il);
            } else {
                *d++ = 0;
                *d++ = 0;
            }
        }
    }

    st_unaligned16(htob_16(d - el - 2), el);
    st_unaligned16(htob_16(d - ml + 2), ml);

    crc = mpeg_crc32(p, d - p);
    st_unaligned32(htob_32(crc), d);
    d += 4;

    return d - p;
}

extern int
mpegps_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    struct mpegps_mux *psm = p->private;
    struct mpegps_output_stream *os;
    int pesflags = 0;
    uint8_t *data;
    int size;
    uint64_t dts;

    if(!pk->data){
        tc2_print("MPEGPS-MUX", TC2_PRINT_DEBUG,
                  "stream %i end\n", pk->stream);
        if(!--psm->nstreams){
            /* write end code */
        }
        psm->streams[pk->stream].dts = -1LL;
        tcfree(pk);
        return 0;
    }

    data = pk->data[0];
    size = pk->sizes[0];
    os = psm->streams + pk->stream;

    tc2_print("MPEGPS-MUX", TC2_PRINT_DEBUG + 1,
              "stream %i, id %x\n", pk->stream, os->stream_id);

    if(pk->flags & TCVP_PKT_FLAG_PTS){
        if(pk->flags & TCVP_PKT_FLAG_DTS)
            dts = pk->dts;
        else
            dts = pk->pts;

        if(dts - os->last_pts > psm->pts_interval){
            if(pk->flags & TCVP_PKT_FLAG_PTS)
                pesflags |= PES_FLAG_PTS;
            if(pk->flags & TCVP_PKT_FLAG_DTS)
                pesflags |= PES_FLAG_DTS;
            os->last_pts = dts;
        }

        os->dts = dts;
    } else {
        dts = os->dts;
    }

    while(size > 0){
        int hl, psize, pl;
        u_char hdr[1024];

        psize = min(size, psm->pessize);

        if(psm->psm){
            hl = write_psm(hdr, psm, 1024);
            psm->out->write(hdr, 1, hl, psm->out);
            psm->psm = 0;
        }

        hl = write_pack_header(hdr, dts, psm->bitrate / (8 * 50));
        psm->out->write(hdr, 1, hl, psm->out);

        if(psm->syshdr){
            hl = write_system_header(hdr, psm);
            psm->out->write(hdr, 1, hl, psm->out);
            psm->syshdr = 0;
        }

        pl = psize;
        if(os->stream_id == PRIVATE_STREAM_1)
            pl += 4;
        hl = write_pes_header(hdr, os->stream_id,
                              pl, pesflags, pk->pts / 300,
                              pk->dts / 300);
        psm->out->write(hdr, 1, hl, psm->out);

        if(os->stream_id == PRIVATE_STREAM_1){
            char b[4] = { os->ac3id, 1, 0, 2 };
            psm->out->write(b, 1, 4, psm->out);
        }
        psm->out->write(data, 1, psize, psm->out);

        data += psize;
        size -= psize;
        pesflags = 0;
    }

    tcfree(pk);

    return 0;
}

extern int
mpegps_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    struct mpegps_mux *psm = p->private;
    const struct mpeg_stream_type *str_type = mpeg_stream_type(s->common.codec);

    if(!str_type)
        return PROBE_FAIL;

    if(psm->astreams <= s->common.index){
        int ns = s->common.index + 1;
        psm->streams = realloc(psm->streams, ns * sizeof(*psm->streams));
        memset(psm->streams + psm->astreams, 0,
               (ns - psm->astreams) * sizeof(*psm->streams));
        psm->astreams = ns;
    }

    psm->streams[s->common.index].str = s;
    psm->streams[s->common.index].stream_type = str_type->mpeg_stream_type;
    psm->streams[s->common.index].last_pts = -1;

    if(s->stream_type == STREAM_TYPE_VIDEO){
        psm->streams[s->common.index].stream_id = psm->vid++;
        psm->nvideo++;
    } else if(s->stream_type == STREAM_TYPE_AUDIO){
        if(!strcmp(s->common.codec, "audio/ac3")){
            psm->streams[s->common.index].stream_id = PRIVATE_STREAM_1;
            psm->streams[s->common.index].ac3id = psm->ac3id++;
        } else {
            psm->streams[s->common.index].stream_id = psm->aid++;
        }
        psm->naudio++;
    }

    psm->bitrate += s->common.bit_rate?: 320000;
    psm->nstreams++;
    psm->psm = 1;
    psm->syshdr = 1;

    p->format.common.bit_rate = psm->bitrate;

    return PROBE_OK;
}

static void
pmx_free(void *p)
{
    struct mpegps_mux *psm = p;

    psm->out->close(psm->out);
    if(psm->streams)
        free(psm->streams);
}

extern int
mpegps_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
           muxed_stream_t *ms)
{
    struct mpegps_mux *psm;
    char *url;
    url_t *out;

    if(tcconf_getvalue(cs, "mux/url", "%s", &url) <= 0){
        tc2_print("MPEGPS-MUX", TC2_PRINT_ERROR, "No output specified.\n");
        return -1;
    }

    if(!(out = url_open(url, "w"))){
        tc2_print("MPEGPS-MUX", TC2_PRINT_ERROR, "Error opening %s.\n", url);
        return -1;
    }

    psm = tcallocdz(sizeof(*psm), NULL, pmx_free);
    psm->out = out;
    psm->vid = 0xe0;
    psm->aid = 0xc0;
    psm->ac3id = 0x80;
    psm->pessize = tcvp_demux_mpeg_conf_pes_size;
    psm->pts_interval = tcvp_demux_mpeg_conf_pts_interval;

    tcconf_getvalue(cs, "pes_size", "%i", &psm->pessize);
    tcconf_getvalue(cs, "pts_interval", "%i", &psm->pts_interval);

    psm->pts_interval *= 27000;

    if(!psm->pessize || psm->pessize > 0xfff2){
        tc2_print("MPEGPS", TC2_PRINT_WARNING,
                  "invalid pes_size %i, using default\n", psm->pessize);
        psm->pessize = 0xfff2;
    }

    p->format.stream_type = STREAM_TYPE_MULTIPLEX;
    p->format.common.codec = "mpeg-ps";
    p->private = psm;

    free(url);

    return 0;
}

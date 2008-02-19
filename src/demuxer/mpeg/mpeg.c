/**
    Copyright (C) 2003-2007  Michael Ahlberg, Måns Rullgård

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
#include <stdarg.h>
#include <tcvp_types.h>
#include <tcvp_bits.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

extern int
mpegpes_header(struct mpegpes_packet *pes, u_char *data, int h)
{
    int hl, pkl = 0;
    const u_char *pts = NULL, *dts = NULL;
    u_char c;

    data -= h;

    if(h == 0)
	if((htob_32(unaligned32(data)) >> 8) != 1)
	    return -1;

    if(h < 4)
	pes->stream_id = data[3];
    if(h < 5)
	pkl = htob_16(unaligned16(data+4));

    c = data[6];
    if((c & 0xc0) == 0x80){
	hl = data[8] + 9;
	if(data[7] & 0x80){
	    pts = data + 9;
	}
	if(data[7] & 0x40){
	    dts = data + 14;
	}
    } else {
	hl = 6;
	while(c == 0xff)
	    c = data[++hl];
	if((c & 0xc0) == 0x40){
	    hl += 2;
	    c = data[hl];
	}
	if((c & 0xe0) == 0x20){
	    pts = data + hl;
	    hl += 4;
	}
	if((c & 0xf0) == 0x30){
	    hl += 5;
	}
	hl++;
    }

    pes->flags = 0;

    if(pts){
	pes->flags |= PES_FLAG_PTS;
	pes->pts = (htob_16(unaligned16(pts+3)) & 0xfffe) >> 1;
	pes->pts |= (htob_16(unaligned16(pts+1)) & 0xfffe) << 14;
	pes->pts |= (uint64_t) (*pts & 0xe) << 29;
/* 	tc2_print("MPEGPS", TC2_PRINT_DEBUG, "stream %x, pts %lli\n", */
/* 		  pes->stream_id, pes->pts); */
    }

    if(dts){
	pes->flags |= PES_FLAG_DTS;
	pes->dts = (htob_16(unaligned16(dts+3)) & 0xfffe) >> 1;
	pes->dts |= (htob_16(unaligned16(dts+1)) & 0xfffe) << 14;
	pes->dts |= (uint64_t) (*dts & 0xe) << 29;
/* 	fprintf(stderr, "MPEGPS: stream %x, dts %lli\n", */
/* 		pes->stream_id, pes->dts); */
    }

    pes->data = data + hl;
    if(pkl)
	pes->size = pkl + 6;

    return 0;
}

extern void
mpeg_free(muxed_stream_t *ms)
{
    if(ms->streams)
	free(ms->streams);
    if(ms->used_streams)
	free(ms->used_streams);
}

extern muxed_stream_t *
mpeg_open(char *name, url_t *u, tcconf_section_t *cs, tcvp_timer_t *t)
{
    muxed_stream_t *ms;

    ms = mpegps_open(name, u, cs, t);
    if(!ms)
	ms = mpegts_open(name, u, cs, t);
    if(!ms)
	return NULL;

    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));

    return ms;
}

const struct mpeg_stream_type mpeg_stream_types[] = {
    { 0x01, 0xe0, "video/mpeg"  },
    { 0x02, 0xe0, "video/mpeg2" },
    { 0x03, 0xc0, "audio/mpeg"  },
    { 0x03, 0xc0, "audio/mp2"   },
    { 0x03, 0xc0, "audio/mp3"   },
    { 0x04, 0xc0, "audio/mpeg"  },
    { 0x0f, 0xc0, "audio/aac"   },
    { 0x10, 0xe0, "video/mpeg4" },
    { 0x11, 0xc0, "audio/aac"   },
    { 0x1b, 0xe0, "video/h264"  },
    { }
};

static const struct mpeg_stream_type hdmv_stream_types[] = {
    { 0x82, EXTENDED_STREAM_ID, "audio/dts" },
    { }
};

static const tcfraction_t frame_rates[16] = {
    { 0,     0    },
    { 24000, 1001 },
    { 24,    1    },
    { 25,    1    },
    { 30000, 1001 },
    { 30,    1    },
    { 50,    1    },
    { 60000, 1001 },
    { 60,    1    }
};

static const tcfraction_t aspect_ratios[16] = {
    [2] = { 4,   3   },
    [3] = { 16,  9   },
    [4] = { 221, 100 }
};

static const struct {
    uint32_t tag;
    char *codec;
    const struct mpeg_stream_type *stream_types;
} reg_desc_tags[] = {
    { 0x41432d33, "audio/ac3" },
    { 0x41565356, "video/cavs" },
    { 0x44545331, "audio/dts" },
    { 0x44545332, "audio/dts" },
    { 0x44545332, "audio/dts" },
    { 0x56432d31, "video/vc1" },
    { 0x48444d56, NULL,       hdmv_stream_types },
    { 0, NULL }
};

extern const struct mpeg_stream_type *
mpeg_stream_type(char *codec)
{
    int i;

    for(i = 0; mpeg_stream_types[i].codec; i++)
	if(!strcmp(codec, mpeg_stream_types[i].codec))
	    return &mpeg_stream_types[i];

    for(i = 0; i < tcvp_demux_mpeg_conf_private_type_count; i++)
	if(!strcmp(codec, tcvp_demux_mpeg_conf_private_type[i].codec))
	    return (struct mpeg_stream_type *)
		tcvp_demux_mpeg_conf_private_type + i;

    return NULL;
}

static int
frame_rate_index(const tcfraction_t *f)
{
    int i;

    for(i = 0; i < 16; i++)
	if(f->num == frame_rates[i].num &&
	   f->den == frame_rates[i].den)
	    return i;

    return 0;
}

static int
aspect_ratio_index(const tcfraction_t *f)
{
    int i;

    for(i = 0; i < 16; i++)
	if(f->num == aspect_ratios[i].num &&
	   f->den == aspect_ratios[i].den)
	    return i;

    return 0;
}

static int
dvb_descriptor(stream_t *s, const uint8_t *d, unsigned int tag,
               unsigned int len)
{
    switch(tag){
    case DVB_AC_3_DESCRIPTOR:
        s->audio.codec = "audio/ac3";
        break;
    case DVB_DTS_DESCRIPTOR:
        s->audio.codec = "audio/dts";
        break;
    case DVB_AAC_DESCRIPTOR:
        s->audio.codec = "audio/aac";
        break;
    }

    return 0;
}

#define DESCR_MPEG2 0
#define DESCR_MPEG4 1

static unsigned
get_mpeg4_size(const u_char **d, const u_char *end)
{
    unsigned v = 0;

    do {
        v <<= 7;
        v += **d & 0x7f;
    } while (*d < end && *(*d)++ & 0x80);

    return v;
}

static int
parse_descriptors(muxed_stream_t *ms, stream_t *s, void *es,
                  const u_char *d, unsigned size,
                  int (*parser)(muxed_stream_t *, stream_t *, void*,
                                const u_char *, const u_char *end),
                  int type)
{
    const u_char *p = d;

    while (size > 1) {
        unsigned dl;

        if (type == DESCR_MPEG4) {
            const u_char *q = p + 1;
            dl = get_mpeg4_size(&q, d + size);
            dl += q - p;
        } else {
            dl = p[1] + 2;
        }

        if (dl > size)
            break;
        parser(ms, s, es, p, p + dl);
        p += dl;
        size -= dl;
    }

    return p - d;
}

static int mpeg4_descriptor(muxed_stream_t *ms, stream_t *s, void *es,
                            const u_char *d, const u_char *end);

static int
mpeg4_es_descriptor(muxed_stream_t *ms, const u_char *d, unsigned size)
{
    struct mpeg_common *mp = ms->private;
    struct mpeg4_es *es;
    unsigned stream_dep_flag;
    unsigned url_flag;
    unsigned ocr_stream_flag;
    unsigned stream_priority;
    unsigned dep_es_id;
    unsigned ocr_es_id;

    if (size < 3)
        return -1;

    tc2_print("MPEG", TC2_PRINT_DEBUG, "MPEG4 ES_Descriptor\n");

    mp->num_mpeg4_es++;
    mp->mpeg4_es = realloc(mp->mpeg4_es,
                           mp->num_mpeg4_es * sizeof(*mp->mpeg4_es));
    es = mp->mpeg4_es + mp->num_mpeg4_es - 1;
    memset(es, 0, sizeof(*es));

    es->es_id = htob_16(unaligned16(d));
    d += 2;

    stream_dep_flag = *d >> 7;
    url_flag        = (*d >> 6) & 1;
    ocr_stream_flag = (*d >> 5) & 1;
    stream_priority = *d & 0x1f;
    d++;

    size -= 3;

    tc2_print("MPEG", TC2_PRINT_DEBUG, "  ES_ID %x\n", es->es_id);
    tc2_print("MPEG", TC2_PRINT_DEBUG, "  streamDependenceFlag %d\n",
              stream_dep_flag);
    tc2_print("MPEG", TC2_PRINT_DEBUG, "  URL_Flag             %d\n",
              url_flag);
    tc2_print("MPEG", TC2_PRINT_DEBUG, "  OCRstreamFlag        %d\n",
              ocr_stream_flag);
    tc2_print("MPEG", TC2_PRINT_DEBUG, "  streamPriority       %d\n",
              stream_priority);

    if (stream_dep_flag) {
        dep_es_id = htob_16(unaligned16(d));
        d += 2;
        size -= 2;
    }

    if (url_flag) {
        unsigned url_length = *d++;
        d += url_length;
        size -= url_length;
    }

    if (ocr_stream_flag) {
        ocr_es_id = htob_16(unaligned16(d));
        d += 2;
        size -= 2;
    }

    parse_descriptors(ms, NULL, es, d, size, mpeg4_descriptor, DESCR_MPEG4);

    return 0;
}

static void
sl_config_descriptor(struct mpeg4_es *es, const uint8_t *d, unsigned size)
{
    struct tcvp_bits bits;
    tcvp_bits_init(&bits, d, size);

    es->sl.predefined = tcvp_bits_get(&bits, 8);

    tc2_print("MPEG", TC2_PRINT_DEBUG, "SLConfigDescriptor: predefined %x\n",
              es->sl.predefined);

    switch (es->sl.predefined) {
    case 0:
        es->sl.useAccessUnitStartFlag       = tcvp_bits_get(&bits, 1);
        es->sl.useAccessUnitEndFlag         = tcvp_bits_get(&bits, 1);
        es->sl.useRandomAccessPointFlag     = tcvp_bits_get(&bits, 1);
        es->sl.hasRandomAccessUnitsOnlyFlag = tcvp_bits_get(&bits, 1);
        es->sl.usePaddingFlag               = tcvp_bits_get(&bits, 1);
        es->sl.useTimeStampsFlag            = tcvp_bits_get(&bits, 1);
        es->sl.useIdleFlag                  = tcvp_bits_get(&bits, 1);
        es->sl.durationFlag                 = tcvp_bits_get(&bits, 1);
        es->sl.timeStampResolution          = tcvp_bits_get(&bits, 32);
        es->sl.OCRResolution                = tcvp_bits_get(&bits, 32);
        es->sl.timeStampLength              = tcvp_bits_get(&bits, 8);
        es->sl.OCRLength                    = tcvp_bits_get(&bits, 8);
        es->sl.AU_Length                    = tcvp_bits_get(&bits, 8);
        es->sl.instantBitrateLength         = tcvp_bits_get(&bits, 8);
        es->sl.degradationPriorityLength    = tcvp_bits_get(&bits, 4);
        es->sl.AU_seqNumLength              = tcvp_bits_get(&bits, 5);
        es->sl.packetSeqNumLength           = tcvp_bits_get(&bits, 5);

        if (es->sl.durationFlag) {
            es->sl.timeScale                = tcvp_bits_get(&bits, 32);
            es->sl.accessUnitDuration       = tcvp_bits_get(&bits, 16);
            es->sl.compositionUnitDuration  = tcvp_bits_get(&bits, 16);
        }

        if (!es->sl.useTimeStampsFlag) {
            es->sl.startDecodingTimeStamp =
                tcvp_bits_get(&bits, es->sl.timeStampLength);
            es->sl.startCompositionTimeStamp =
                tcvp_bits_get(&bits, es->sl.timeStampLength);
        }
        break;

    case 1:
        es->sl.timeStampResolution = 1000;
        es->sl.timeStampLength = 32;
        break;

    case 2:
        es->sl.useTimeStampsFlag = 1;
        break;

    default:
        tc2_print("MPEG", TC2_PRINT_WARNING,
                  "unknown SLConfig predefined valued %x\n",
                  es->sl.predefined);
        break;
    }

#define slprint(n) tc2_print("MPEG", TC2_PRINT_DEBUG, "  %-30s %d\n", #n, \
                             es->sl.n)

    slprint(useAccessUnitStartFlag);
    slprint(useAccessUnitEndFlag);
    slprint(useRandomAccessPointFlag);
    slprint(hasRandomAccessUnitsOnlyFlag);
    slprint(usePaddingFlag);
    slprint(useTimeStampsFlag);
    slprint(useIdleFlag);
    slprint(durationFlag);
    slprint(timeStampResolution);
    slprint(OCRResolution);
    slprint(timeStampLength);
    slprint(OCRLength);
    slprint(AU_Length);
    slprint(instantBitrateLength);
    slprint(degradationPriorityLength);
    slprint(AU_seqNumLength);
    slprint(packetSeqNumLength);
    slprint(timeScale);
    slprint(accessUnitDuration);
    slprint(compositionUnitDuration);
}

static int
mpeg4_descriptor(muxed_stream_t *ms, stream_t *s, void *p, const u_char *d,
                 const u_char *end)
{
    struct mpeg4_es *es = p;
    unsigned tag = *d++;
    unsigned len = get_mpeg4_size(&d, end);

    tc2_print("MPEG", TC2_PRINT_DEBUG,
              "MPEG4 descriptor %3d [%2x], %x bytes\n", tag, tag, len);

    switch (tag) {
    case ES_DESCRTAG:
        mpeg4_es_descriptor(ms, d, len);
        break;

    case DECODERCONFIGDESCRTAG:
        if (len < 2)
            break;

        es->objectType = *d++;
        es->streamType = *d >> 2;

        tc2_print("MPEG", TC2_PRINT_DEBUG, "  objectTypeIndication %x\n",
                  es->objectType);
        tc2_print("MPEG", TC2_PRINT_DEBUG, "  streamType           %x\n",
                  es->streamType);
        tc2_print("MPEG", TC2_PRINT_DEBUG, "  upStream             %d\n",
                  (*d >> 1) & 1);
        break;

    case SLCONFIGDESCRTAG:
        sl_config_descriptor(es, d, len);
        break;
    }

    return 0;
}

static int
initial_object_descriptor(muxed_stream_t *ms, const u_char *d, unsigned len)
{
    unsigned od_id, url_flag, ipl_flag;
    unsigned val;
    unsigned size;
    const u_char *end = d + len;

    d++;

    size = get_mpeg4_size(&d, end);
    if (d + size > end)
        return -1;

    val = htob_16(unaligned16(d));
    d += 2;
    size -= 2;

    od_id = val >> 6;
    url_flag = (val >> 5) & 1;
    ipl_flag = (val >> 4) & 1;

    tc2_print("MPEG", TC2_PRINT_DEBUG, "InitialObjectDescriptor\n");
    tc2_print("MPEG", TC2_PRINT_DEBUG,
              "  ObjectDescriptorID             %2x\n", od_id);
    tc2_print("MPEG", TC2_PRINT_DEBUG,
              "  URL_Flag                       %2d\n", url_flag);
    tc2_print("MPEG", TC2_PRINT_DEBUG,
              "  includeInlineProfileLevelFlag  %2d\n", ipl_flag);

    if (url_flag) {
        unsigned url_length = *d++;
        d += url_length;
        size -= url_length;
    } else {
        unsigned od_pl       = *d++;
        unsigned scene_pl    = *d++;
        unsigned audio_pl    = *d++;
        unsigned visual_pl   = *d++;
        unsigned graphics_pl = *d++;
        size -= 5;

        tc2_print("MPEG", TC2_PRINT_DEBUG,
                  "  ODProfileLevelIndication       %2x\n", od_pl);
        tc2_print("MPEG", TC2_PRINT_DEBUG,
                  "  sceneProfileLevelIndication    %2x\n", scene_pl);
        tc2_print("MPEG", TC2_PRINT_DEBUG,
                  "  audioProfileLevelIndication    %2x\n", audio_pl);
        tc2_print("MPEG", TC2_PRINT_DEBUG,
                  "  visualProfileLevelIndication   %2x\n", visual_pl);
        tc2_print("MPEG", TC2_PRINT_DEBUG,
                  "  graphicsProfileLevelIndication %2x\n", graphics_pl);

        parse_descriptors(ms, NULL, NULL, d, size, mpeg4_descriptor,
                          DESCR_MPEG4);
    }

    return 0;
}

static struct mpeg4_es *
find_mpeg4_es(const struct mpeg_common *m, unsigned es_id)
{
    unsigned i;

    for (i = 0; i < m->num_mpeg4_es; i++)
        if (m->mpeg4_es[i].es_id == es_id)
            return m->mpeg4_es + i;

    return NULL;
}

static int
mpeg_descriptor(muxed_stream_t *ms, stream_t *s, void *p, const u_char *d,
                const u_char *end)
{
    struct mpeg_common *mp = ms->private;
    struct mpeg_stream_common *es = p;
    int tag = d[0];
    int len = d[1];
    unsigned int i;
    unsigned int v;

    tc2_print("MPEG", TC2_PRINT_DEBUG, "descriptor %3d [%2x], %d bytes\n",
              tag, tag, len);

    switch(tag){
    case VIDEO_STREAM_DESCRIPTOR:
        if (len < 1)
            break;

	s->video.frame_rate = frame_rates[(d[2] >> 3) & 0xf];
#if 0
	if(d[2] & 0x4)
	    fprintf(stderr, "MPEG: MPEG 1 only\n");
	if(d[2] & 0x2)
	    fprintf(stderr, "MPEG: constrained parameter\n");
	if(!(d[2] & 0x4)){
	    fprintf(stderr, "MPEG: esc %i profile %i, level %i\n",
		    d[3] >> 7, (d[3] >> 4) & 0x7, d[3] & 0xf);
	}
#endif
	break;

    case AUDIO_STREAM_DESCRIPTOR:
	break;

    case TARGET_BACKGROUND_GRID_DESCRIPTOR:
        if (len < 4)
            break;

	v = htob_32(unaligned32(d + 2));

	s->video.width = (v >> 18) & 0x3fff;
	s->video.height = (v >> 4) & 0x3fff;

	v &= 0xf;
	if(v == 1){
	    s->video.aspect.num = s->video.width;
	    s->video.aspect.den = s->video.height;
	    tcreduce(&s->video.aspect);
	} else if(aspect_ratios[v].num){
	    s->video.aspect = aspect_ratios[v];
	}
	break;

    case ISO_639_LANGUAGE_DESCRIPTOR:
        if(len > 3)
            memcpy(s->audio.language, d + 2, 3);
	break;

    case REGISTRATION_DESCRIPTOR:
        if (len < 4)
            break;

        v = htob_32(unaligned32(d + 2));
        for(i = 0; reg_desc_tags[i].tag; i++){
            if(reg_desc_tags[i].tag == v){
                if (s && reg_desc_tags[i].codec) {
                    s->common.codec = reg_desc_tags[i].codec;
                } else if (reg_desc_tags[i].stream_types) {
                    if (es)
                        es->stream_types = reg_desc_tags[i].stream_types;
                    else
                        mp->stream_types = reg_desc_tags[i].stream_types;
                }
                break;
            }
        }
        tc2_print("MPEG", TC2_PRINT_DEBUG, "  registration_descriptor: "
                  "format_identifier %08x\n", v);
        break;

    case IOD_DESCRIPTOR:
        if (len < 2)
            break;

        tc2_print("MPEG", TC2_PRINT_DEBUG,
                  "IOD_descriptor: scope=%x IOD_label=%x\n", d[2], d[3]);
        d += 4;
        initial_object_descriptor(ms, d, len - 2);
        break;

    case SL_DESCRIPTOR:
        if (len < 2)
            break;

        v = htob_16(unaligned16(d + 2));
        es->mpeg4_es = find_mpeg4_es(mp, v);
        tc2_print("MPEG", TC2_PRINT_DEBUG, "SL_descriptor: ES_ID=%x\n", v);
        break;

    case FMC_DESCRIPTOR:
        tc2_print("MPEG", TC2_PRINT_DEBUG, "FMC_descriptor\n");
        d += 2;

        if (len == 2) {
            es->mpeg4_es = find_mpeg4_es(mp, htob_16(unaligned16(d)));
        }

        while (len >= 3) {
            tc2_print("MPEG", TC2_PRINT_DEBUG,
                      "  ES_ID=%x FlexMuxChannel=%x\n",
                      htob_16(unaligned16(d)), d[2]);
            d += 3;
            len -= 3;
        }
        break;
    }

    if(tag >= 64){
        if(tcvp_demux_mpeg_conf_dvb)
            dvb_descriptor(s, d, tag, len);
    }

    return len + 2;
}

extern int
mpeg_parse_descriptors(muxed_stream_t *ms, stream_t *s, void *p,
                       const u_char *d, unsigned size)
{
    return parse_descriptors(ms, s, p, d, size, mpeg_descriptor, DESCR_MPEG2);
}

extern int
write_mpeg_descriptor(stream_t *s, int tag, u_char *d, int size)
{
    u_char *p = d;
    int i;

    switch(tag){
    case VIDEO_STREAM_DESCRIPTOR:
	if(size < 5)
	    return 0;
	i = frame_rate_index(&s->video.frame_rate);
	if(!i)
	    return 0;
	*p++ = tag;
	*p++ = 3;
	*p++ = i << 3;
	*p++ = 0x48;
	*p++ = 0x5f;
	return 5;

    case TARGET_BACKGROUND_GRID_DESCRIPTOR:
	if(size < 6)
	    return 0;
	if(!s->video.width || !s->video.height)
	    return 0;
	if(!s->video.aspect.num){
	    i = 1;
	} else {
	    i = aspect_ratio_index(&s->video.aspect);
	    if(!i){
		tcfraction_t f = { s->video.width, s->video.height };
		tcreduce(&f);
		if(f.num == s->video.aspect.num &&
		   f.den == s->video.aspect.den)
		    i = 1;
	    }
	}
	if(!i)
	    return 0;
	*p++ = tag;
	*p++ = 4;
	st_unaligned32(htob_32((s->video.width << 18) |
			       (s->video.height << 4) | i),
		       p);
	return 6;
    }

    return 0;
}

extern const struct mpeg_stream_type *
mpeg_stream_type_id(int st, const struct mpeg_stream_type *types)
{
    int i;

    for(i = 0; types[i].codec; i++)
	if(types[i].mpeg_stream_type == st)
	    return types + i;

    for(i = 0; i < tcvp_demux_mpeg_conf_private_type_count; i++)
	if(tcvp_demux_mpeg_conf_private_type[i].id == st)
	    return (struct mpeg_stream_type *)
		tcvp_demux_mpeg_conf_private_type + i;

    return NULL;
}

extern int
write_pes_header(u_char *p, int stream_id, int size, int flags, ...)
{
    va_list args;
    u_char *plen;
    int hdrl = 0, pklen = size;

    va_start(args, flags);

    st_unaligned32(htob_32(stream_id | 0x100), p);
    p += 4;
    plen = p;
    p += 2;

    if(stream_id != PADDING_STREAM &&
       stream_id != PRIVATE_STREAM_2 &&
       stream_id != ECM_STREAM &&
       stream_id != EMM_STREAM &&
       stream_id != PROGRAM_STREAM_DIRECTORY &&
       stream_id != DSMCC_STREAM &&
       stream_id != H222_E_STREAM &&
       stream_id != SYSTEM_HEADER){
	int pflags = 0;
	uint64_t pts = 0, dts = 0;

	pklen += 3;
	*p++ = 0x80;

	if(flags & PES_FLAG_PTS){
	    pts = va_arg(args, uint64_t);
	    pflags |= 0x80;
	    hdrl += 5;
	}

	if(flags & PES_FLAG_DTS){
	    dts = va_arg(args, uint64_t);
	    pflags |= 0x40;
	    hdrl += 5;
	}

	*p++ = pflags;
	*p++ = hdrl;

	if(flags & PES_FLAG_PTS){
	    *p++ = ((pts >> 29) & 0xe) | (flags & PES_FLAG_DTS? 0x31: 0x21);
	    st_unaligned16(htob_16(((pts >> 14) & 0xfffe) | 1), p);
	    p += 2;
	    st_unaligned16(htob_16(((pts << 1) & 0xfffe) | 1), p);
	    p += 2;
	}

	if(flags & PES_FLAG_DTS){
	    *p++ = ((dts >> 29) & 0xe) | 0x11;
	    st_unaligned16(htob_16(((dts >> 14) & 0xfffe) | 1), p);
	    p += 2;
	    st_unaligned16(htob_16(((dts << 1) & 0xfffe) | 1), p);
	    p += 2;
	}

	pklen += hdrl;
    }

    if(size > 0){
	if(pklen > 0xffff)
	    tc2_print("MPEG", TC2_PRINT_WARNING, "oversized PES packet: %i\n",
		      pklen);
    } else {
	pklen = 0;
    }

    st_unaligned16(htob_16(pklen), plen);

    return hdrl + 9;
}

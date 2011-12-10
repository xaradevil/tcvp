/**
    Copyright (C) 2003-2005  Michael Ahlberg, Måns Rullgård

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
#include <libavformat/avformat.h>
#include <avf.h>
#include <avformat_tc2.h>

extern int
avf_init(char *p)
{
    av_register_all();
    return 0;
}

static char *codec_names[][2] = {
    { "video/mpeg", "mpeg1video" },
    { "video/mpeg2", "mpeg2video" },
    { "video/dv", "dvvideo" },
    { "video/msmpeg4v3", "msmpeg4" },
    { "audio/mpeg", "mp3" },
    { "audio/dts", "dca" },
    { }
};

extern char *
avf_codec_avname(const char *codec)
{
    char *cn;
    int i;

    for(i = 0; codec_names[i][0]; i++){
        if(!strcmp(codec, codec_names[i][0])){
            return strdup(codec_names[i][1]);
        }
    }

    cn = strchr(codec, '/');
    if(!cn)
        return NULL;

    cn = strdup(cn + 1);

    for(i = 0; cn[i]; i++)
        if(cn[i] == '-')
            cn[i] = '_';

    return cn;
}

static char *
avf_codec_name(int codec)
{
    AVCodec *avc;
    char *cn;
    int i;

    avc = avcodec_find_decoder(codec);
    if(!avc)
        return NULL;

    for(i = 0; codec_names[i][0]; i++){
        if(!strcmp(avc->name, codec_names[i][1])){
            return strdup(codec_names[i][0]);
        }
    }

    cn = malloc(6 + strlen(avc->name) + 1);
    if(!cn)
        return NULL;

    sprintf(cn, "%s/%s", avc->type == AVMEDIA_TYPE_AUDIO? "audio": "video",
            avc->name);

    for(i = 6; cn[i]; i++)
        if(cn[i] == '_')
            cn[i] = '-';

    return cn;
}


typedef struct {
    AVFormatContext *afc;
} avf_stream_t;

typedef struct avf_packet {
    tcvp_data_packet_t pk;
    AVPacket apk;
    u_char *data;
    int size;
} avf_packet_t;

static void
avf_free_packet(void *v)
{
    avf_packet_t *ap = v;
    av_free_packet(&ap->apk);
    free(ap->data);
}

extern tcvp_packet_t *
avf_next_packet(muxed_stream_t *ms, int stream)
{
    avf_stream_t *as = ms->private;
    AVFormatContext *afc = as->afc;
    avf_packet_t *pk;
    int sx;

    pk = tcallocdz(sizeof(*pk), NULL, avf_free_packet);
    av_init_packet(&pk->apk);

    do {
        int rp;
#if LIBAVFORMAT_BUILD < 4610
        rp = av_read_packet(afc, &pk->apk);
#else
        rp = av_read_frame(afc, &pk->apk);
#endif
        if(rp < 0){
            tcfree(pk);
            return NULL;
        }

        sx = pk->apk.stream_index;

        if(!ms->used_streams[sx])
            av_free_packet(&pk->apk);
    } while(!ms->used_streams[sx]);

    pk->pk.stream = sx;
    pk->pk.sizes = &pk->size;
    pk->pk.data = &pk->data;
    pk->data = malloc(pk->apk.size + 16);
    pk->size = pk->apk.size;
    memcpy(pk->data, pk->apk.data, pk->size);
    memset(pk->data + pk->size, 0, 16);
    pk->pk.planes = 1;
    pk->pk.flags = 0;
    if(pk->apk.pts != AV_NOPTS_VALUE){
        pk->pk.flags |= TCVP_PKT_FLAG_PTS;
#if LIBAVCODEC_BUILD > 4753
        pk->pk.pts = pk->apk.pts * 27000000LL *
            afc->streams[sx]->time_base.num /
            afc->streams[sx]->time_base.den;
#else
        pk->pk.pts = pk->apk.pts * 27000000 / AV_TIME_BASE;
#endif
        tc2_print("AVFORMAT", TC2_PRINT_DEBUG+1, "[%i] pts %lli\n",
                  sx, pk->pk.pts);
    }
#if LIBAVFORMAT_BUILD >= 4610
    if(pk->apk.dts != AV_NOPTS_VALUE){
        pk->pk.flags |= TCVP_PKT_FLAG_DTS;
#if LIBAVCODEC_BUILD > 4753
        pk->pk.dts = pk->apk.dts * 27000000LL *
            afc->streams[sx]->time_base.num /
            afc->streams[sx]->time_base.den;
#else
        pk->pk.dts = pk->apk.dts * 27000000 / AV_TIME_BASE;
#endif
    }
#endif

    return (tcvp_packet_t *) pk;
}

extern void
avf_free(void *p)
{
    muxed_stream_t *ms = p;
    avf_stream_t *as = ms->private;

    av_close_input_file(as->afc);
    free(ms->streams);
    free(ms->used_streams);

    free(as);
}

static uint64_t
avf_seek(muxed_stream_t *ms, uint64_t time)
{
    avf_stream_t *as = ms->private;
    int64_t avtime = AV_TIME_BASE * time / 27000000LL;

#ifdef AVSEEK_FLAG_BACKWARD
    if(av_seek_frame(as->afc, -1, avtime, AVSEEK_FLAG_BACKWARD) < 0)
        return -1;
#else
    if(av_seek_frame(as->afc, -1, avtime) < 0)
        return -1;
#endif

    return time;
}

extern muxed_stream_t *
avf_open(char *name, url_t *u, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    AVFormatContext *afc = NULL;
    muxed_stream_t *ms;
    avf_stream_t *as;
    int i;

    if(avformat_open_input(&afc, name, NULL, NULL) != 0){
        tc2_print("AVFORMAT", TC2_PRINT_ERROR, "Error opening %s\n", name);
        return NULL;
    }

    if(avformat_find_stream_info(afc, NULL) < 0){
        tc2_print("AVFORMAT", TC2_PRINT_ERROR,
                  "Can't find stream info for %s\n", name);
        return NULL;
    }

    ms = tcallocd(sizeof(*ms), NULL, avf_free);
    memset(ms, 0, sizeof(*ms));
    ms->n_streams = afc->nb_streams;
    ms->streams = calloc(ms->n_streams, sizeof(*ms->streams));
    for(i = 0; i < ms->n_streams; i++){
        AVStream *avs = afc->streams[i];
        stream_t *st = ms->streams + i;

        if(AVCODEC(avs, codec_id) == CODEC_ID_NONE)
            continue;

        switch(AVCODEC(afc->streams[i], codec_type)){
        case AVMEDIA_TYPE_VIDEO:
            st->stream_type = STREAM_TYPE_VIDEO;
#if LIBAVFORMAT_BUILD > 4623
            st->video.frame_rate.num = avs->r_frame_rate.num;
            st->video.frame_rate.den = avs->r_frame_rate.den;
#else
            st->video.frame_rate.num = avs->r_frame_rate;
            st->video.frame_rate.den = avs->r_frame_rate_base;
#endif
            st->video.width = AVCODEC(avs, width);
            st->video.height = AVCODEC(avs, height);

            tc2_print("AVFORMAT", TC2_PRINT_DEBUG, "[%i] codec_tag %x\n",
                      i, AVCODEC(avs, codec_tag));
            tc2_print("AVFORMAT", TC2_PRINT_DEBUG,
                      "[%i] stream_codec_tag %x\n",
                      i, AVCODEC(avs, stream_codec_tag));
            break;

        case AVMEDIA_TYPE_AUDIO:
            st->stream_type = STREAM_TYPE_AUDIO;
            st->audio.sample_rate = AVCODEC(avs, sample_rate);
            st->audio.channels = AVCODEC(avs, channels);
            st->audio.bit_rate = AVCODEC(avs, bit_rate);
            st->audio.block_align = AVCODEC(avs, block_align);
            break;

        default:
            st->stream_type = 0;
            continue;
            break;
        }

        st->common.codec = avf_codec_name(AVCODEC(avs, codec_id));

        if(!st->common.codec)
            tc2_print("AVFORMAT", TC2_PRINT_WARNING,
                      "[%i] unknown codec id %i\n", i,
                      AVCODEC(avs, codec_id));
        else
            tc2_print("AVFORMAT", TC2_PRINT_DEBUG, "[%i] codec_id %x -> %s\n",
                      i, AVCODEC(avs, codec_id),
                      st->common.codec);

        st->common.codec_data = AVCODEC(avs, extradata);
        st->common.codec_data_size = AVCODEC(avs, extradata_size);
        tc2_print("AVFORMAT", TC2_PRINT_DEBUG, "[%i] codec_data_size %i\n",
                  i, st->common.codec_data_size);

        st->common.index = i;
    }

    ms->time = 27000000LL * afc->duration / AV_TIME_BASE;

    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));
    ms->next_packet = avf_next_packet;
    ms->seek = avf_seek;

    as = calloc(1, sizeof(*as));
    as->afc = afc;

    ms->private = as;

    return ms;
}

/**
    Copyright (C) 2005  Michael Ahlberg, Måns Rullgård

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
#include <fnmatch.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcmath.h>
#include <tcvp_types.h>
#include <matroska_tc2.h>
#include "ebml.h"
#include "matroska.h"

typedef struct matroska_seek {
    uint64_t info;
    uint64_t tracks;
    uint64_t cues;
} matroska_seek_t;

typedef struct matroska_info {
    uint64_t timecodescale;
    double duration;
    char *title;
    char *muxingapp;
    char *writingapp;
} matroska_info_t;

typedef struct matroska_video {
    int interlaced;
    u_int stereomode;
    u_int pixelwidth;
    u_int pixelheight;
    u_int pixelcropbottom;
    u_int pixelcroptop;
    u_int pixelcropleft;
    u_int pixelcropright;
    u_int displaywidth;
    u_int displayheight;
    u_int displayunit;
    u_int aspectratiotype;
} matroska_video_t;

typedef struct matroska_audio {
    double samplingfrequency;
    double outputsamplingfrequency;
    u_int channels;
    u_int bitdepth;
} matroska_audio_t;

typedef struct matroska_track {
    u_int number;
    u_int type;
#define MATROSKA_TRACK_TYPE_VIDEO    0x1
#define MATROSKA_TRACK_TYPE_AUDIO    0x2
#define MATROSKA_TRACK_TYPE_COMPLEX  0x3
#define MATROSKA_TRACK_TYPE_LOGO     0x10
#define MATROSKA_TRACK_TYPE_SUBTITLE 0x11
#define MATROSKA_TRACK_TYPE_BUTTONS  0x12
#define MATROSKA_TRACK_TYPE_CONTROL  0x20

    int enabled;
    int lacing;
    uint64_t defaultduration;
    double timecodescale;
    char *name;
    char *lang;
    char *codecid;
    char *codecprivate;
    u_int codecprivate_size;
    char *codecname;
    char *codecsettings;
    matroska_video_t video;
    matroska_audio_t audio;
} matroska_track_t;

typedef struct matroska_block {
    uint64_t track;
    int16_t time;
    u_int flags;
    u_int frames;
    u_int size;
    u_int *fsizes;
    u_char *data;
    u_char *framedata;
    int frame;
} matroska_block_t;

typedef struct matroska {
    url_t *u;
    uint64_t segment_start;
    uint64_t cluster_start;
    int *map;
    int mapsize;
    matroska_seek_t seek;
    matroska_info_t info;
    matroska_track_t *tracks;
    int ntracks;

    uint64_t clustertime;
    matroska_block_t block;
} matroska_t;

typedef struct matroska_packet {
    tcvp_data_packet_t pk;
    u_char *data, *buf;
    int size;
} matroska_packet_t;

typedef struct matroska_codec {
    char *codecid;
    char *codec;
    int (*setup)(matroska_track_t *, stream_t *);
} matroska_codec_t;

typedef struct matroska_ptr {
    matroska_t *msk;
    void *p;
} matroska_ptr_t;

static matroska_codec_t msk_codecs[];

static int
xiph_int(url_t *u, int *vs)
{
    int val = 0, v;

    if(vs)
	*vs = 0;

    do {
	v = url_getc(u);
	val += v;
	if(vs)
	    (*vs)++;
    } while(v == 255);

    return val;
}

static int
xiph_int_mem(u_char *p, int size, int *vs)
{
    int val = 0, v;

    if(vs)
	*vs = 0;

    while(size > 0){
	v = *p++;
	val += v;
	if(vs)
	    (*vs)++;
	if(v != 255)
	    break;
	size--;
    }

    return val;
}

static int
msk_cb_seek(uint64_t id, uint64_t size, void *p)
{
    matroska_ptr_t *mp = p;
    matroska_t *msk = mp->msk;
    uint64_t *sip = mp->p;

    switch(id){
    case MATROSKA_ID_SEEKID:
	sip[0] = ebml_get_vint(msk->u, NULL);
	if(sip[0] == -1)
	    return EBML_CB_ERROR;
	break;

    case MATROSKA_ID_SEEKPOSITION:
	sip[1] = ebml_get_int(msk->u, size);
	if(sip[1] == -1)
	    return EBML_CB_ERROR;
	break;

    default:
	return EBML_CB_UNKNOWN;
    }

    return EBML_CB_SUCCESS;
}

static int
msk_read_seek(matroska_t *msk, uint64_t psize)
{
    uint64_t sip[2] = { -1, -1 };
    matroska_ptr_t mp = { msk, sip };

    if(ebml_read_elements(msk->u, psize, msk_cb_seek, &mp))
	return -1;

    if(sip[0] == -1){
	tc2_print("MATROSKA", TC2_PRINT_ERROR, "seekid undefined\n");
	return -1;
    }

    if(sip[1] == -1){
	tc2_print("MATROSKA", TC2_PRINT_ERROR, "seekpos undefined\n");
	return -1;
    }

    tc2_print("MATROSKA", TC2_PRINT_DEBUG+1, "seek %llx @%llx\n",
	      sip[0], sip[1]);

    switch(sip[0]){
    case MATROSKA_ID_INFO:
	msk->seek.info = sip[1];
	break;
    case MATROSKA_ID_TRACKS:
	msk->seek.tracks = sip[1];
	break;
    case MATROSKA_ID_CUES:
	msk->seek.cues = sip[1];
	break;
    }

    return 0;
}

static int
msk_cb_seekhead(uint64_t id, uint64_t size, void *p)
{
    matroska_t *msk = p;

    switch(id){
    case MATROSKA_ID_SEEK:
	if(msk_read_seek(msk, size))
	    return EBML_CB_ERROR;
	break;
    default:
	return EBML_CB_UNKNOWN;
	break;
    }

    return EBML_CB_SUCCESS;
}

static int
msk_cb_info(uint64_t id, uint64_t size, void *p)
{
    matroska_t *msk = p;

    switch(id){
    case MATROSKA_ID_TIMECODESCALE:
	msk->info.timecodescale = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_DURATION:
	msk->info.duration = ebml_get_float(msk->u, size);
	break;
    case MATROSKA_ID_TITLE:
	msk->info.title = ebml_get_string(msk->u, size);
	break;
    case MATROSKA_ID_MUXINGAPP:
	msk->info.muxingapp = ebml_get_string(msk->u, size);
	break;
    case MATROSKA_ID_WRITINGAPP:
	msk->info.writingapp = ebml_get_string(msk->u, size);
	break;
    default:
	return EBML_CB_UNKNOWN;
    }

    return EBML_CB_SUCCESS;
}

static int
msk_track_defaults(matroska_track_t *mt)
{
    memset(mt, 0, sizeof(*mt));

    mt->enabled = 1;
    mt->lacing = 1;
    mt->timecodescale = 1.0;

    mt->audio.samplingfrequency = 8000.0;
    mt->audio.outputsamplingfrequency = 8000.0;
    mt->audio.channels = 1;

    return 0;
}

static int
msk_cb_video(uint64_t id, uint64_t size, void *p)
{
    matroska_ptr_t *mp = p;
    matroska_t *msk = mp->msk;
    matroska_track_t *mt = mp->p;

    switch(id){
    case MATROSKA_ID_FLAGINTERLACED:
	mt->video.interlaced = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_STEREOMODE:
	mt->video.stereomode = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_PIXELWIDTH:
	mt->video.pixelwidth = ebml_get_int(msk->u, size);
	mt->video.displaywidth = mt->video.pixelwidth;
	break;
    case MATROSKA_ID_PIXELHEIGHT:
	mt->video.pixelheight = ebml_get_int(msk->u, size);
	mt->video.displayheight = mt->video.pixelheight;
	break;
    case MATROSKA_ID_PIXELCROPBOTTOM:
	mt->video.pixelcropbottom = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_PIXELCROPTOP:
	mt->video.pixelcroptop = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_PIXELCROPLEFT:
	mt->video.pixelcropleft = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_PIXELCROPRIGHT:
	mt->video.pixelcropright = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_DISPLAYWIDTH:
	mt->video.displaywidth = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_DISPLAYHEIGHT:
	mt->video.displayheight = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_ASPECTRATIOTYPE:
	mt->video.aspectratiotype = ebml_get_int(msk->u, size);
	break;
    default:
	return EBML_CB_UNKNOWN;
    }

    return EBML_CB_SUCCESS;
}

static int
msk_cb_audio(uint64_t id, uint64_t size, void *p)
{
    matroska_ptr_t *mp = p;
    matroska_t *msk = mp->msk;
    matroska_track_t *mt = mp->p;

    switch(id){
    case MATROSKA_ID_SAMPLINGFREQUENCY:
	mt->audio.samplingfrequency = ebml_get_float(msk->u, size);
	mt->audio.outputsamplingfrequency = mt->audio.samplingfrequency;
	break;
    case MATROSKA_ID_OUTPUTSAMPLINGFREQUENCY:
	mt->audio.outputsamplingfrequency = ebml_get_float(msk->u, size);
	break;
    case MATROSKA_ID_CHANNELS:
	mt->audio.channels = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_BITDEPTH:
	mt->audio.bitdepth = ebml_get_int(msk->u, size);
	break;
    default:
	return EBML_CB_UNKNOWN;
    }

    return EBML_CB_SUCCESS;
}

static int
msk_cb_trackentry(uint64_t id, uint64_t size, void *p)
{
    matroska_ptr_t *mp = p;
    matroska_t *msk = mp->msk;
    matroska_track_t *mt = mp->p;

    switch(id){
    case MATROSKA_ID_TRACKNUMBER:
	mt->number = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_TRACKTYPE:
	mt->type = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_FLAGENABLED:
	mt->enabled = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_FLAGLACING:
	mt->lacing = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_DEFAULTDURATION:
	mt->defaultduration = ebml_get_int(msk->u, size);
	break;
    case MATROSKA_ID_TRACKTIMECODESCALE:
	mt->timecodescale = ebml_get_float(msk->u, size);
	break;
    case MATROSKA_ID_NAME:
	mt->name = ebml_get_string(msk->u, size);
	break;
    case MATROSKA_ID_LANGUAGE:
	mt->lang = ebml_get_string(msk->u, size);
	break;
    case MATROSKA_ID_CODECID:
	mt->codecid = ebml_get_string(msk->u, size);
	break;
    case MATROSKA_ID_CODECPRIVATE:
	mt->codecprivate = ebml_get_binary(msk->u, size);
	mt->codecprivate_size = size;
	break;
    case MATROSKA_ID_CODECNAME:
	mt->codecname = ebml_get_string(msk->u, size);
	break;
    case MATROSKA_ID_CODECSETTINGS:
	mt->codecsettings = ebml_get_string(msk->u, size);
	break;
    case MATROSKA_ID_VIDEO:
	if(ebml_read_elements(msk->u, size, msk_cb_video, mp))
	    return EBML_CB_ERROR;
	break;
    case MATROSKA_ID_AUDIO:
	if(ebml_read_elements(msk->u, size, msk_cb_audio, mp))
	    return EBML_CB_ERROR;
	break;
    default:
	return EBML_CB_UNKNOWN;
    }

    return EBML_CB_SUCCESS;
}

static int
msk_cb_tracks(uint64_t id, uint64_t size, void *p)
{
    matroska_t *msk = p;

    switch(id){
    case MATROSKA_ID_TRACKENTRY: {
	int nt = msk->ntracks + 1;
	matroska_track_t *mt;
	matroska_ptr_t mp;

	msk->tracks = realloc(msk->tracks, nt * sizeof(*msk->tracks));
	mt = msk->tracks + msk->ntracks;
	msk->ntracks = nt;
	msk_track_defaults(mt);

	mp.msk = msk;
	mp.p = mt;

	if(ebml_read_elements(msk->u, size, msk_cb_trackentry, &mp))
	    return EBML_CB_ERROR;
	break;
    }
    default:
	return EBML_CB_UNKNOWN;
    }

    return EBML_CB_SUCCESS;
}

static matroska_codec_t *
msk_find_codec(char *id)
{
    int i;

    for(i = 0; msk_codecs[i].codecid; i++){
	if(!fnmatch(msk_codecs[i].codecid, id, 0))
	    return msk_codecs + i;
    }

    return NULL;
}

static int
msk_setup_stream(matroska_t *msk, matroska_track_t *mt, stream_t *st)
{
    matroska_codec_t *mc = msk_find_codec(mt->codecid);

    if(mt->number >= msk->mapsize){
	msk->mapsize = mt->number + 1;
	msk->map = realloc(msk->map, msk->mapsize * sizeof(*msk->map));
    }

    tc2_print("MATROSKA", TC2_PRINT_DEBUG, "track %i -> %i\n",
	      mt->number, st->common.index);
    msk->map[mt->number] = st->common.index;

    if(!mc){
	tc2_print("MATROSKA", TC2_PRINT_WARNING,
		  "unknown codecid %s for track %i@%i\n",
		  mt->codecid, mt->number, st->common.index);
	return -1;
    }

    st->common.codec = mc->codec;
    st->common.codec_data = mt->codecprivate;
    st->common.codec_data_size = mt->codecprivate_size;

    if(mt->type == MATROSKA_TRACK_TYPE_VIDEO){
	st->stream_type = STREAM_TYPE_VIDEO;
	st->video.width = mt->video.pixelwidth;
	st->video.height = mt->video.pixelheight;
	st->video.frame_rate.num = 1000000;
	st->video.frame_rate.den = mt->defaultduration / 1000;
	tcreduce(&st->video.frame_rate);
	st->video.aspect.num = mt->video.displaywidth;
	st->video.aspect.den = mt->video.displayheight;
	tcreduce(&st->video.aspect);
    } else if(mt->type == MATROSKA_TRACK_TYPE_AUDIO){
	st->stream_type = STREAM_TYPE_AUDIO;
	st->audio.sample_rate = mt->audio.samplingfrequency;
	st->audio.channels = mt->audio.channels;
	if(mt->lang){
	    strncpy(st->audio.language, mt->lang, 4);
	    st->audio.language[3] = 0;
	}
    } else if(mt->type == MATROSKA_TRACK_TYPE_SUBTITLE){
	st->stream_type = STREAM_TYPE_SUBTITLE;
	if(mt->lang){
	    strncpy(st->subtitle.language, mt->lang, 4);
	    st->subtitle.language[3] = 0;
	}
    }

    if(mc->setup){
	if(mc->setup(mt, st))
	    return -1;
    }

    return 0;
}

static int
msk_create_streams(muxed_stream_t *ms)
{
    matroska_t *msk = ms->private;
    int i;

    tc2_print("MATROSKA", TC2_PRINT_DEBUG, "%i tracks\n", msk->ntracks);

    ms->n_streams = msk->ntracks;
    ms->streams = calloc(msk->ntracks, sizeof(*ms->streams));
    ms->used_streams = calloc(msk->ntracks, sizeof(*ms->used_streams));

    for(i = 0; i < msk->ntracks; i++){
	matroska_track_t *mt = msk->tracks + i;
	stream_t *st = ms->streams + i;
	st->common.index = i;
	msk_setup_stream(msk, mt, st);
    }

    return 0;
}

static int
msk_block(matroska_t *msk, uint64_t size)
{
    matroska_block_t *mb = &msk->block;
    int ss;

    mb->data = tcalloc(size);
    if(!mb->data)
	return -1;

    mb->size = size;
    mb->track = ebml_get_vint(msk->u, &ss);
    mb->size -= ss;
    url_get16b(msk->u, &mb->time);
    mb->size -= 2;
    mb->flags = url_getc(msk->u);
    mb->size--;

    tc2_print("MATROSKA", TC2_PRINT_DEBUG+3,
	      "block on track %lli [%i], flags %x\n",
	      mb->track, msk->map[mb->track], mb->flags);

    if(mb->flags & 0x6){
	mb->frames = url_getc(msk->u) + 1;
	mb->size--;
	mb->fsizes = calloc(mb->frames, sizeof(*mb->fsizes));

	if(mb->flags & 0x2){
	    int i, ts = 0;
	    for(i = 0; i < mb->frames - 1; i++){
		if(mb->flags & 0x4){
		    int fs = ebml_get_vint(msk->u, &ss);
		    if(i){
			fs -= (1 << (8 * ss - ss - 1)) - 1;
			fs += mb->fsizes[i - 1];
		    }
		    mb->fsizes[i] = fs;
		} else {
		    mb->fsizes[i] = xiph_int(msk->u, &ss);
		}
		ts += mb->fsizes[i];
		mb->size -= ss;
	    }
	    mb->fsizes[i] = mb->size - ts;
	} else {
	    int i;
	    for(i = 0; i < mb->frames; i++)
		mb->fsizes[i] = mb->size / mb->frames;
	}
    } else {
	mb->frames = 1;
	mb->fsizes = calloc(mb->frames, sizeof(*mb->fsizes));
	mb->fsizes[0] = mb->size;
    }

    mb->framedata = mb->data;
    mb->frame = 0;

    if(msk->u->read(mb->data, 1, mb->size, msk->u) < mb->size)
	return -1;

    return 0;
}

static void
msk_free_block(matroska_block_t *mb)
{
    free(mb->fsizes);
    mb->fsizes = NULL;
    tcfree(mb->data);
    mb->data = NULL;
    mb->frames = 0;
    mb->frame = 0;
}

static void
msk_free_pk(void *p)
{
    matroska_packet_t *mp = p;
    tcfree(mp->buf);
}

static int
msk_cb_blockgroup(uint64_t id, uint64_t size, void *p)
{
    matroska_t *msk = p;

    if(id == MATROSKA_ID_BLOCK){
	if(msk_block(msk, size) < 0)
	    return EBML_CB_ERROR;
    } else {
	return EBML_CB_UNKNOWN;
    }

    return EBML_CB_SUCCESS;
}

static tcvp_packet_t *
msk_packet(muxed_stream_t *ms, int str)
{
    matroska_t *msk = ms->private;
    matroska_packet_t *pk;
    matroska_track_t *mt;
    int trackidx;

    while(!msk->block.frames){
	uint64_t id, size;
	if(ebml_element(msk->u, &id, &size, NULL))
	    return NULL;

	switch(id){
	case MATROSKA_ID_CLUSTER:
	    break;
	case MATROSKA_ID_TIMECODE:
	    msk->clustertime = ebml_get_int(msk->u, size);
	    break;
	case MATROSKA_ID_BLOCKGROUP:
	    if(ebml_read_elements(msk->u, size, msk_cb_blockgroup, msk))
		return NULL;
	    if(!ms->used_streams[msk->map[msk->block.track]])
		msk_free_block(&msk->block);
	    break;
	default:
	    msk->u->seek(msk->u, size, SEEK_CUR);
	    break;
	}
    }

    trackidx = msk->map[msk->block.track];
    mt = msk->tracks + trackidx;

    tc2_print("MATROSKA", TC2_PRINT_DEBUG+2, "packet on track %lli [%i]\n",
	      msk->block.track, trackidx);

    pk = tcallocdz(sizeof(*pk), NULL, msk_free_pk);
    pk->pk.type = TCVP_PKT_TYPE_DATA;
    pk->pk.stream = trackidx;
    pk->pk.data = &pk->data;
    pk->pk.sizes = &pk->size;
    pk->pk.planes = 1;
    if(!msk->block.frame){
	pk->pk.flags |= TCVP_PKT_FLAG_PTS;
	pk->pk.pts = (msk->clustertime + msk->block.time) *
	    msk->info.timecodescale * mt->timecodescale * 27 / 1000;
	tc2_print("MATROSKA", TC2_PRINT_DEBUG+1, "track %lli pts %lli\n",
		  msk->block.track, pk->pk.pts / 27000);
    }

    pk->buf = tcref(msk->block.data);
    pk->data = msk->block.framedata;
    pk->size = msk->block.fsizes[msk->block.frame];

    msk->block.framedata += pk->size;
    msk->block.frame++;

    if(msk->block.frame == msk->block.frames)
	msk_free_block(&msk->block);

    return (tcvp_packet_t *) pk;
}

static void
msk_free(void *p)
{
    muxed_stream_t *ms = p;
    matroska_t *msk = ms->private;
    int i;

    tcfree(msk->u);
    free(msk->map);
    free(msk->info.title);
    free(msk->info.muxingapp);
    free(msk->info.writingapp);

    for(i = 0; i < msk->ntracks; i++){
	matroska_track_t *mt = msk->tracks + i;
	free(mt->name);
	free(mt->lang);
	free(mt->codecid);
	free(mt->codecprivate);
	free(mt->codecname);
	free(mt->codecsettings);
    }

    free(msk->tracks);
    msk_free_block(&msk->block);
    free(msk);

    free(ms->streams);
    free(ms->used_streams);
}

extern muxed_stream_t *
msk_open(char *name, url_t *u, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    muxed_stream_t *ms = NULL;
    matroska_t *msk;
    ebml_header_t eh;
    uint64_t id, size;

    if(ebml_readheader(u, &eh) < 0){
	tc2_print("MATROSKA", TC2_PRINT_ERROR, "failed reading EBML header\n");
	goto err;
    }

    if(strcmp(eh.doctype, "matroska")){
	tc2_print("MATROSKA", TC2_PRINT_ERROR, "unknown doctype '%s'\n",
		  eh.doctype);
	goto err;
    }

    msk = calloc(1, sizeof(*msk));
    if(!msk)
	goto err;
    msk->u = tcref(u);

    while(!msk->cluster_start && !ebml_element(u, &id, &size, NULL)){
	ebml_element_callback_t ecb = NULL;

	switch(id){
	case MATROSKA_ID_SEGMENT:
	    msk->segment_start = u->tell(u);
	    break;

	case MATROSKA_ID_SEEKHEAD:
	    ecb = msk_cb_seekhead;
	    break;

	case MATROSKA_ID_INFO:
	    msk->info.timecodescale = 1000000;
	    ecb = msk_cb_info;
	    break;

	case MATROSKA_ID_TRACKS:
	    ecb = msk_cb_tracks;
	    break;

	case MATROSKA_ID_CLUSTER:
	    msk->cluster_start = u->tell(u);
	    break;

	default:
	    u->seek(u, size, SEEK_CUR);
	    break;
	}

	if(ecb && ebml_read_elements(u, size, ecb, msk) < 0)
	    goto err;
    }

    tc2_print("MATROSKA", TC2_PRINT_DEBUG, "title %s\n", msk->info.title);
    tc2_print("MATROSKA", TC2_PRINT_DEBUG, "timecodescale %lli\n",
	      msk->info.timecodescale);
    tc2_print("MATROSKA", TC2_PRINT_DEBUG, "duration %lf (%lf ns)\n",
	      msk->info.duration,
	      msk->info.duration * msk->info.timecodescale);
    tc2_print("MATROSKA", TC2_PRINT_DEBUG, "muxingapp %s\n",
	      msk->info.muxingapp);
    tc2_print("MATROSKA", TC2_PRINT_DEBUG, "writingapp %s\n",
	      msk->info.writingapp);

    ms = tcallocdz(sizeof(*ms), NULL, msk_free);
    if(!ms)
	goto err;
    ms->private = msk;
    ms->time = msk->info.duration * msk->info.timecodescale * 27 / 1000;
    ms->next_packet = msk_packet;

    msk_create_streams(ms);

    if(msk->info.title)
	tcattr_set(ms, "title", msk->info.title, NULL, NULL);

    u->seek(u, msk->cluster_start, SEEK_SET);

  out:
    free(eh.doctype);
    return ms;

  err:
    tcfree(ms);
    ms = NULL;
    goto out;
}

static int
msk_codec_vorbis(matroska_track_t *mt, stream_t *st)
{
    u_char *p = mt->codecprivate, *cdp;
    int cps = mt->codecprivate_size;
    int nf, ss, s[3], cds;
    int i;

    if(cps < 3)
	return -1;

    nf = *p++;
    cps--;

    if(nf != 2)
	return -1;

    for(i = 0; i < 2; i++){
	s[i] = xiph_int_mem(p, cps, &ss);
	p += ss;
	cps -= ss;
    }

    s[2] = cps - s[0] - s[1];

    cds = cps + 6;
    cdp = malloc(cds);
    st->common.codec_data = cdp;
    st->common.codec_data_size = cds;

    for(i = 0; i < 3; i++){
	*cdp++ = s[i] >> 8;
	*cdp++ = s[i] & 0xff;
	memcpy(cdp, p, s[i]);
	cdp += s[i];
	p += s[i];
	cps -= s[i];
    }

    return 0;
}

static int
msk_codec_qt_video(matroska_track_t *mt, stream_t *st)
{
    return 0;
}

static int
msk_codec_qt_audio(matroska_track_t *mt, stream_t *st)
{
    return 0;
}

static int
msk_codec_pcm(matroska_track_t *mt, stream_t *st)
{
    return 0;
}

static int
msk_codec_msfcc(matroska_track_t *mt, stream_t *st)
{
    if(mt->codecprivate_size < 20)
	return -1;

    st->common.codec = video_x_msvideo_vcodec(mt->codecprivate + 16);

    return 0;
}

static int
msk_codec_aac(matroska_track_t *mt, stream_t *st)
{
    static const char *profiles[] =
	{ "MAIN", "LC", "SSR", "LTP", "LC/SBR", NULL };

    static const int sampling_freqs[] = {
	92017,
	75132,
	55426,
	46009,
	37566,
	27713,
	23004,
	18783,
	13856,
	11502,
	9391,
	0
    };

    char *profile = mt->codecid + 12;
    int size = 5;
    u_char *d = malloc(size);
    int pridx, sfidx;

    st->common.codec_data = d;
    st->common.codec_data_size = size;

    for(pridx = 0; profiles[pridx]; pridx++)
	if(!strcmp(profile, profiles[pridx]))
	    break;

    if(!profiles[pridx])
	return -1;

    pridx++;

    for(sfidx = 0; mt->audio.samplingfrequency < sampling_freqs[sfidx];
	sfidx++);

    if(!sampling_freqs[sfidx])
	return -1;

    *d++ = (pridx << 3) | (sfidx >> 1);
    *d = (sfidx << 7) | (mt->audio.channels << 3);

    if(pridx == 5){
	mt->audio.samplingfrequency *= 2;
	st->audio.sample_rate = mt->audio.samplingfrequency;
	for(sfidx = 0; mt->audio.samplingfrequency < sampling_freqs[sfidx];
	    sfidx++);
	*d++ |= sfidx >> 1;
	*d++ = (sfidx << 7) | pridx;
    }

    return 0;
}

static matroska_codec_t msk_codecs[] = {
    { .codecid = "V_MPEG1",
      .codec   = "video/mpeg" },
    { .codecid = "V_MPEG2",
      .codec   = "video/mpeg2" },
    { .codecid = "V_MPEG4/ISO/AVC",
      .codec   = "video/h264" },
    { .codecid = "V_MPEG4/ISO/*",
      .codec   = "video/mpeg4" },
    { .codecid = "V_MPEG4/MS/V3",
      .codec   = "video/msmpeg4v3" },
    { .codecid = "V_REAL/RV10",
      .codec   = "video/rv10" },
    { .codecid = "V_REAL/RV20",
      .codec   = "video/rv20" },
    { .codecid = "V_REAL/RV30",
      .codec   = "video/rv30" },
    { .codecid = "V_REAL/RV40",
      .codec   = "video/rv40" },
    { .codecid = "V_QUICKTIME",
      .setup   = msk_codec_qt_video },
    { .codecid = "V_MS/VFW/FOURCC",
      .setup   = msk_codec_msfcc },

    { .codecid = "A_MPEG/L3",
      .codec   = "audio/mp3" },
    { .codecid = "A_MPEG/L2",
      .codec   = "audio/mp2" },
    { .codecid = "A_MPEG/L1",
      .codec   = "audio/mp1" },
    { .codecid = "A_AAC/MPEG[24]/*",
      .codec   = "audio/aac",
      .setup   = msk_codec_aac },
    { .codecid = "A_PCM/*/*",
      .setup   = msk_codec_pcm },
    { .codecid = "A_AC3",
      .codec   = "audio/ac3" },
    { .codecid = "A_AC3/*",
      .codec   = "audio/ac3" },
    { .codecid = "A_DTS",
      .codec   = "audio/dts" },
    { .codecid = "A_VORBIS",
      .codec   = "audio/vorbis",
      .setup   = msk_codec_vorbis },
    { .codecid = "A_FLAC",
      .codec   = "audio/flac" },
    { .codecid = "A_REAL/14_4",
      .codec   = "audio/real_144" },
    { .codecid = "A_REAL/28_8",
      .codec   = "audio/real_288" },
    { .codecid = "A_REAL/COOK",
      .codec   = "audio/real_cook" },
    { .codecid = "A_REAL/SIPR",
      .codec   = "audio/sipro" },
    { .codecid = "A_REAL/RALF",
      .codec   = "audio/ralf" },
    { .codecid = "A_REAL/ATRC",
      .codec   = "audio/atrac3" },
    { .codecid = "A_QUICKTIME/*",
      .setup   = msk_codec_qt_audio },

    { .codecid = "S_TEXT/UTF8",
      .codec   = "subtitle/text" },
    { .codecid = "S_TEXT/SSA",
      .codec   = "subtitle/ssa" },
    { .codecid = "S_TEXT/ASS",
      .codec   = "subtitle/ass" },
    { .codecid = "S_VOBSUB",
      .codec   = "subtitle/dvd" },

    { }
};

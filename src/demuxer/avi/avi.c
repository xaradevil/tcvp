/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**/

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <ctype.h>
#include <tclist.h>
#include <pthread.h>
#include <stdint.h>
#include <avi_tc2.h>

#undef DEBUG

static char *vtag2codec(char *tag);
static char *aid2codec(int id);

#define TAG(a,b,c,d) (a + (b << 8) + (c << 16) + (d << 24))

#define get4c(t,f) fread(&t, 4, 1, f)

#ifdef DEBUG
#define getjunk(f,s) do {			\
    uint##s##_t v = getu##s(f);			\
    printf("    %x %i\n", v, v);		\
} while(0)

#define getval(f, n, s) printf("    " n ": %i\n", getu##s(f));
#else
#define getjunk(f,s) getu##s(f)
#define getval(f,n,s) getu##s(f)
#endif

static inline uint32_t
getu32(FILE *f)
{
    uint32_t v;
    fread(&v, 4, 1, f);
    return v;
}

static inline uint32_t
getu16(FILE *f)
{
    uint16_t v;
    fread(&v, 2, 1, f);
    return v;
}

typedef struct avi_index {
    char tag[4];
    uint32_t flags;
    uint32_t offset;
    uint32_t size;
} avi_index_t;

typedef struct avi_file {
    FILE *file;
    list **packets;
    pthread_mutex_t mx;
    int eof;
    int pkt;
    avi_index_t *index;
    int idxlen;
    int idxok;
} avi_file_t;

static muxed_stream_t *
avi_header(FILE *f)
{
    muxed_stream_t *ms = NULL;
    avi_file_t *af;
    uint32_t tag, size, next = 0, stype = 0, sidx = -1;
    uint32_t width = 0, height = 0;
    uint32_t ftime = 0, start = 0;
    char st[5] = {[4] = 0};
    int i;
    long movi_start = 0;

    if(fread(&tag, 4, 1, f) != 1)
	return NULL;

    if(tag != TAG('R','I','F','F'))
	return NULL;

    getu32(f);			/* size */

    tag = getu32(f);
    if(tag != TAG('A','V','I',' '))
	return NULL;

    ms = malloc(sizeof(*ms));
    af = calloc(1, sizeof(*af));
    af->file = f;
    pthread_mutex_init(&af->mx, NULL);
    ms->private = af;

    while(fread(&tag, 4, 1, f) == 1){
	size = getu32(f);
	size += size & 1;

#ifdef DEBUG
	*(uint32_t *)st = tag;
	fprintf(stderr, "%s:\n", st);
#endif

	if(next && tag != next){
	    printf("AVI: error unexpected tag\n");
	    return NULL;
	}

	switch(tag){
	case TAG('L','I','S','T'): {
	    uint32_t lt = getu32(f);
	    if(lt == TAG('m','o','v','i')){
		movi_start = ftell(f);
		fseek(f, size-4, SEEK_CUR);
	    } else if(lt == TAG('s','t','r','l')){
		next = TAG('s','t','r','h');
	    }
	    break;
	}
	case TAG('a','v','i','h'):{
	    size_t fp = ftell(f);

	    ftime = getu32(f);

	    getjunk(f, 32);
	    getjunk(f, 32);
	    getval(f, "flags", 32);
	    getval(f, "frames", 32);
	    getjunk(f, 32);

	    ms->n_streams = getu32(f);
	    ms->streams = calloc(ms->n_streams, sizeof(*ms->streams));
	    af->packets = calloc(ms->n_streams, sizeof(list *));
	    for(i = 0; i < ms->n_streams; i++)
		af->packets[i] = list_new(TC_LOCK_NONE);

	    getjunk(f, 32);

	    width = getu32(f);
	    height = getu32(f);

	    fseek(f, fp + size, SEEK_SET);
	    break;
	}
	case TAG('s','t','r','h'):{
	    size_t fp = ftell(f);
	    uint32_t frn, frd;

	    sidx++;

	    stype = getu32(f);
	    get4c(st, f);	/* tag */
	    getval(f, "flags", 32);
	    getval(f, "prio", 16);
	    getval(f, "lang", 16);
	    getval(f, "init frame", 32);

	    if(stype == TAG('v','i','d','s')){
		ms->streams[sidx].stream_type = STREAM_TYPE_VIDEO;
		frd = getu32(f);
		frn = getu32(f);
		if(frn && frd){
		    ms->streams[sidx].video.frame_rate.num = frn;
		    ms->streams[sidx].video.frame_rate.den = frd;
		} else {
		    ms->streams[sidx].video.frame_rate.num = 1000000;
		    ms->streams[sidx].video.frame_rate.den = ftime;
		}

		start = getu32(f);

		ms->streams[sidx].video.frames = getu32(f);

		getval(f, "bufsize", 32);
		getval(f, "quality", 32);
		getval(f, "sample size", 32);
		getjunk(f, 16);
		getjunk(f, 16);

		ms->streams[sidx].video.width = getu16(f);
		ms->streams[sidx].video.height = getu16(f);
		if(!ms->streams[sidx].video.width){
		    ms->streams[sidx].video.width = width;
		    ms->streams[sidx].video.height = height;
		}
	    } else if(stype == TAG('a','u','d','s')){
		ms->streams[sidx].stream_type = STREAM_TYPE_AUDIO;
		getval(f, "scale", 32);
		getval(f, "rate", 32);
		start = getu32(f);
		getval(f, "length", 32);
	    }

	    if(start){
		fprintf(stderr, "AVI: start = %i\n", start);
	    }

	    fseek(f, fp + size, SEEK_SET);
	    next = TAG('s','t','r','f');
	    break;
	}
	case TAG('s','t','r','f'):{
	    size_t fp = ftell(f);
	    size_t cds;
	    switch(stype){
	    case TAG('v','i','d','s'):
		getval(f, "size", 32);

		width = getu32(f);
		height = getu32(f);
		if(!ms->streams[sidx].video.width){
		    ms->streams[sidx].video.width = width;
		    ms->streams[sidx].video.height = height;
		}

		getval(f, "panes", 16);
		getval(f, "depth", 16);

		get4c(st, f);
		ms->streams[sidx].video.codec = vtag2codec(st);

		getval(f, "image_size", 32);
		getval(f, "xpelspermeter", 32);
		getval(f, "ypelspermeter", 32);
		getval(f, "clrused", 32);
		getval(f, "clrimportant", 32);

		if((cds = size - 40) > 0){
		    ms->streams[sidx].video.codec_data_size = cds;
		    ms->streams[sidx].video.codec_data = malloc(cds);
		    fread(ms->streams[sidx].video.codec_data, 1, cds, f);
		}
		break;

	    case TAG('a','u','d','s'):{
		int id = getu16(f);
		ms->streams[sidx].audio.codec = aid2codec(id);
		ms->streams[sidx].audio.channels = getu16(f);
		ms->streams[sidx].audio.sample_rate = getu32(f);
		ms->streams[sidx].audio.bit_rate = getu32(f) * 8;
		ms->streams[sidx].audio.block_align = getu16(f);

		if(size > 14)
		    ms->streams[sidx].audio.sample_size = getu16(f);

		if(size > 16){
		    cds = getu16(f);
		    if(cds > 0){
			if(cds > size - 18)
			    cds = size - 18;
			ms->streams[sidx].video.codec_data_size = cds;
			ms->streams[sidx].video.codec_data = malloc(cds);
			fread(ms->streams[sidx].video.codec_data, 1, cds, f);
		    }
		}
		break;
	    }
	    default:
		fprintf(stderr, "AVI: bad stream type\n");
	    }

	    next = 0;
	    fseek(f, fp + size, SEEK_SET);
	    break;
	}
	case TAG('i','d','x','1'):{
	    int idxoff;
	    int idxl = size / sizeof(avi_index_t);
	    af->index = calloc(idxl, sizeof(*af->index));
	    idxl = fread(af->index, sizeof(avi_index_t), idxl, f);
	    af->idxlen = idxl;
	    idxoff = movi_start - af->index[0].offset;
	    for(i = 0; i < idxl; i++){
		af->index[i].offset += idxoff;
	    }
	    break;
	}
	default:
	    fseek(f, size, SEEK_CUR);
	}
    }

    fseek(f, movi_start, SEEK_SET);

    return ms;
}

static void
avi_free_packet(packet_t *p)
{
    free(p->sizes);
    free(p->private);
    free(p);
}

static char xval[] = {
    [' '] = 0x0,
    ['0'] = 0x0,
    ['1'] = 0x1,
    ['2'] = 0x2,
    ['3'] = 0x3,
    ['4'] = 0x4,
    ['5'] = 0x5,
    ['6'] = 0x6,
    ['7'] = 0x7,
    ['8'] = 0x8,
    ['9'] = 0x9,
    ['a'] = 0xa,
    ['b'] = 0xb,
    ['c'] = 0xc,
    ['d'] = 0xd,
    ['e'] = 0xe,
    ['f'] = 0xf,
    ['A'] = 0xa,
    ['B'] = 0xb,
    ['C'] = 0xc,
    ['D'] = 0xd,
    ['E'] = 0xe,
    ['F'] = 0xf
};

static inline int
valid_tag(char *t, int strict)
{
    return (isxdigit(t[0]) || t[0] == ' ') && isxdigit(t[1]) &&
	(strict? (t[2] == 'w' || t[2] == 'd') &&
	 (t[3] > 'a' && t[3] < 'e'):
	 (isalpha(t[2]) && isalpha(t[3])));
}

static inline int
tag2str(u_char *tag)
{
    return (xval[tag[0]] << 4) + xval[tag[1]];    
}

static packet_t *
avi_packet(muxed_stream_t *ms, int stream)
{
    u_char tag[5] = {[4] = 0};
    uint32_t size;
    avi_file_t *af = ms->private;
    int str;
    packet_t *pk = NULL;

    pthread_mutex_lock(&af->mx);

    if(!(pk = list_shift(af->packets[stream])) && !af->eof){
	int tried_index = 0, tried_bkup = 0, skipped = 0, scan = 0;
	do {
	    char *buf;
	    size_t pos;

	    /* FIXME: get rid of gotos */
	again:
	    pos = ftell(af->file);
	    if(!get4c(tag, af->file))
		break;

	    if(!strcmp(tag, "LIST")){
		getu32(af->file); /* size */
		getu32(af->file); /* rec */
		goto again;
	    } else if(!strcmp(tag, "idx1")){
		af->eof = 1;
		break;
	    }

	    if(!valid_tag(tag, scan)){
		if(!scan)
		    fprintf(stderr, "AVI[%i]: Bad packet header @ %08lx\n",
			    stream, pos);
		if(!tried_index && af->idxok > 256){
		    fprintf(stderr, "AVI: Index => %08x\n",
			    af->index[af->pkt].offset);
		    fseek(af->file, af->index[af->pkt].offset, SEEK_SET);
		    tried_index++;
		    goto again;
		} else if(!tried_bkup){
		    fprintf(stderr, "AVI: Backing up 64 bytes.\n");
		    fseek(af->file, -68, SEEK_CUR);
		    tried_bkup++;
		    goto again;
		} else if(scan < 8192){
		    fseek(af->file, -3, SEEK_CUR);
		    scan++;
		    goto again;
		} else if(skipped < 8 && af->idxok > 256){
		    fprintf(stderr, "AVI: Skipping frame.\n");
		    af->pkt++;
		    fseek(af->file, af->index[af->pkt].offset, SEEK_SET);
		    skipped++;
		    goto again;
		}

		fprintf(stderr, "AVI: Can't find valid packet.  Giving up.\n");
		fprintf(stderr, "AVI: %02x%02x%02x%02x:%s @ %16lx\n",
			tag[0], tag[1], tag[2], tag[3], tag, pos);
		af->eof = 1;
		break;
	    }

	    if(af->index){
		if(af->index[af->pkt].offset == pos){
		    af->idxok++;
		} else {
		    af->idxok = 0;
		}
	    }

	    af->pkt++;

	    size = getu32(af->file);

	    str = tag2str(tag);
	    if(!ms->used_streams[str]){
		fseek(af->file, size + (size&1), SEEK_CUR);
		continue;
	    }

	    buf = malloc(size);
	    fread(buf, 1, size, af->file);
	    if(size & 1)
		fgetc(af->file);

	    pk = malloc(sizeof(*pk));
	    pk->private = buf;
	    pk->data = (u_char **) &pk->private;
	    pk->sizes = malloc(sizeof(*pk->sizes));
	    pk->sizes[0] = size;
	    pk->planes = 1;
	    pk->pts = 0;
	    pk->free = avi_free_packet;

	    if(str != stream){
		list_push(af->packets[str], pk);
		pk = NULL;
	    }
	} while(str != stream);
    }

    pthread_mutex_unlock(&af->mx);

    return pk;
}

static int
avi_close(muxed_stream_t *ms)
{
    avi_file_t *af = ms->private;
    int i;

    free(ms->streams);
    free(ms->used_streams);

    for(i = 0; i < ms->n_streams; i++)
	list_destroy(af->packets[i], (tc_free_fn) avi_free_packet);
    free(af->packets);

    fclose(af->file);
    if(af->index)
	free(af->index);
    free(af);

    return 0;
}

extern muxed_stream_t *
avi_open(char *file, conf_section *cs)
{
    FILE *f;
    muxed_stream_t *ms;

    if(!(f = fopen(file, "r")))
	return NULL;

    if(!(ms = avi_header(f)))
	return NULL;

    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));
    ms->next_packet = avi_packet;
    ms->close = avi_close;

    return ms;
}

static char *vcodec_tags[][2] = {
    { "H263", "video/h263" },
    { "I263", "video/h263i" },
    { "MJPG", "video/mjpeg" },
    { "DIVX", "video/mpeg4" },
    { "divx", "video/mpeg4" },
    { "DX50", "video/mpeg4" },
    { "XVID", "video/mpeg4" },
    { "xvid", "video/mpeg4" },
    { "mp4s", "video/mpeg4" },
    { "MP4S", "video/mpeg4" },
    { "M4S2", "video/mpeg4" },
    { "m4s2", "video/mpeg4" },
    { "\x04\0\0\0", "video/mpeg4" },
    { "DIV3", "video/msmpeg4v3" },
    { "div3", "video/msmpeg4v3" },
    { "MP43", "video/msmpeg4v3" },
    { "MP42", "video/msmpeg4v2" },
    { "MPG4", "video/msmpeg4v1" },
    { "WMV1", "video/wmv1" },
    { "dvsl", "video/dvvideo" },
    { "dvsd", "video/dvvideo" },
    { "DVSD", "video/dvvideo" },
    { "dvhd", "video/dvvideo" },
    { "mpg1", "video/mpeg1video" },
    { "mpg2", "video/mpeg1video" },
    { "PIM1", "video/mpeg1video" },
    { "MJPG", "video/mjpeg" },
    { "HFYU", "video/huffyuv" },
    { "hfyu", "video/huffyuv" },
    { "CYUV", "video/cyuv" },
    { "cyuv", "video/cyuv" },
    { NULL, NULL }
};

static char *
vtag2codec(char *tag)
{
    int i;

    for(i = 0; vcodec_tags[i][0]; i++){
	if(!memcmp(tag, vcodec_tags[i][0], 4))
	    return vcodec_tags[i][1];
    }

    return NULL;
}

static char *acodec_ids[][2] = {
    { (char*) 0x50, "audio/mp2" },
    { (char*) 0x55, "audio/mp3" },
    { (char*) 0x2000, "audio/ac3" },
    { (char*) 0x01, "audio/pcm-s16le" },
    { (char*) 0x06, "audio/pcm-alaw" },
    { (char*) 0x07, "audio/pcm-ulaw" },
    { (char*) 0x02, "audio/adpcm-ms" },
    { (char*) 0x11, "audio/adpcm-ima-wav" },
    { (char*) 0x160, "audio/wmav1" },
    { (char*) 0x161, "audio/wmav2" },
    { NULL, NULL },
};

static char *
aid2codec(int id)
{
    int i;

    for(i = 0; acodec_ids[i][0]; i++){
	if(id == (long) acodec_ids[i][0])
	    return acodec_ids[i][1];
    }

    return NULL;
}

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
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <ctype.h>
#include <tclist.h>
#include <pthread.h>
#include <stdint.h>
#include <tcendian.h>
#include <avi_tc2.h>

#define max_skip tcvp_demux_avi_conf_max_skip
#define max_scan tcvp_demux_avi_conf_max_scan
#define backup tcvp_demux_avi_conf_backup
#define starttime tcvp_demux_avi_conf_starttime

#undef DEBUG

static char *vtag2codec(char *tag);
static char *aid2codec(int id);
static int avi_read_indx(muxed_stream_t *ms);
static void avi_free(void *p);

#define TAG(a,b,c,d) (a + (b << 8) + (c << 16) + (d << 24))

#define get4c(t,f) f->read(&t, 4, 1, f)

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

#define getuint(s)				\
static inline uint##s##_t			\
getu##s(url_t *f)				\
{						\
    uint##s##_t v;				\
    url_getu##s##l(f, &v);			\
    return v;					\
}

getuint(16)
getuint(32)
getuint(64)

#define AVI_FLAG_KEYFRAME 0x10

#define AVI_INDEX_SUPER 0
#define AVI_INDEX_CHUNK 1

typedef struct avi_idx1 {
    char tag[4];
    uint32_t flags;
    uint32_t offset;
    uint32_t size;
} avi_idx1_t;

typedef struct avi_index {
    uint64_t pts;
    uint64_t offset;
    uint32_t size;
    uint32_t flags;
} avi_index_t;

typedef struct avi_stream {
    int scale;
    int rate;
    int sample_size;
    int block_align;
    int wavex;
    avi_index_t *index;
    int idxlen, idxsize;
    uint64_t ipts, ptsn, ptsd;
    int pkt;
} avi_stream_t;

typedef struct avi_file {
    url_t *file;
    tclist_t *packets;
    pthread_mutex_t mx;
    int eof;
    int idxok;
    uint32_t movi_start;
    avi_stream_t *streams;
    avi_index_t **index;
    int idxlen;
    int pkt;
    int has_video;
} avi_file_t;

typedef struct avi_packet {
    tcvp_data_packet_t pk;
    uint32_t flags;
    int size;
    u_char *data;
    u_char *buf;
    size_t asize;
} avi_packet_t;

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

static char *
tagstr(char *str, uint32_t tag)
{
    int i;
    for(i = 0; i < 4; i++){
	str[i] = tag & 0xff;
	tag >>= 8;
    }
    str[4] = 0;
    return str;
}

static int
avi_add_index(muxed_stream_t *ms, int s, uint64_t offset,
	      uint32_t size, uint32_t flags)
{
    avi_file_t *af = ms->private;
    avi_stream_t *st = &af->streams[s];
    uint64_t mpts = 1;
    int ixl = st->idxlen;

    if(st->idxlen == st->idxsize){
	st->idxsize = st->idxsize? st->idxsize * 2: 65536;
	st->index = realloc(st->index, st->idxsize * sizeof(*st->index));
    }

    if(ms->streams[s].stream_type == STREAM_TYPE_AUDIO)
	flags &= ~AVI_FLAG_KEYFRAME;

    st->index[ixl].pts = st->ipts / st->ptsd;
    st->index[ixl].offset = offset;
    st->index[ixl].size = size;
    st->index[ixl].flags = flags;

    if(ms->streams[s].stream_type == STREAM_TYPE_AUDIO) {
	if(!st->sample_size){
	    mpts = 1;
	} else if(st->wavex && ms->streams[s].audio.block_align){
	    mpts = size / ms->streams[s].audio.block_align;
	} else {
	    mpts = size / st->sample_size;
	}
    }
    st->ipts += st->ptsn * mpts;

    st->idxlen++;

    return 0;
}

static int
avi_read_idx1(muxed_stream_t *ms, uint32_t size)
{
    avi_file_t *af = ms->private;
    int idxoff = 0;
    int idxl = size / sizeof(avi_idx1_t);
    int i, s;
    avi_idx1_t idx1;

    for(i = 0; i < idxl; i++){
	if(af->file->read(&idx1, sizeof(idx1), 1, af->file) <= 0)
	    break;

	if(!valid_tag(idx1.tag, 0)){
	    if(memcmp(idx1.tag, "rec ", 4))
		tc2_print("AVI", TC2_PRINT_WARNING,
			  "Invalid tag in index @%i\n", i);
	    continue;
	}

	s = tag2str(idx1.tag);
	if(s > ms->n_streams){
	    tc2_print("AVI", TC2_PRINT_WARNING,
		      "Invalid stream #%i in index.\n", s);
	    continue;
	}

	if(i == 0)
	    idxoff = af->movi_start - idx1.offset;

	avi_add_index(ms, s, idx1.offset + idxoff, idx1.size, idx1.flags);
    }

    if(i != idxl)
	tc2_print("AVI", TC2_PRINT_WARNING,
		  "Short index. Expected %i, got %i.\n", idxl, i);

    return 0;
}

static int
avi_read_chunk_indx(muxed_stream_t *ms, int s, uint32_t size)
{
    avi_file_t *af = ms->private;
    uint64_t base;
    int i;

    base = getu64(af->file);
    getu32(af->file);		/* MBZ */

    for(i = 0; i < size; i++){
	uint32_t offset;
	uint32_t size;
	uint32_t flags = 0;

	offset = getu32(af->file);
	size = getu32(af->file);
	if(!(size & (1 << 31)))
	    flags |= AVI_FLAG_KEYFRAME;
	size &= ((1U << 31) - 1);
	avi_add_index(ms, s, base + offset - 8, size, flags);
    }

    return 0;
}

static int
avi_read_super_indx(muxed_stream_t *ms, int s, uint32_t size)
{
    avi_file_t *af = ms->private;
    int i;

    getu32(af->file);		/* MBZ */
    getu32(af->file);
    getu32(af->file);

    for(i = 0; i < size; i++){
	uint64_t offset;
	off_t pos;

	offset = getu64(af->file);
	getu32(af->file);	/* size */
	getu32(af->file);	/* duration */

	pos = af->file->tell(af->file);
	af->file->seek(af->file, offset, SEEK_SET);
	getu32(af->file);	/* tag */
	getu32(af->file);	/* size */
	avi_read_indx(ms);
	af->file->seek(af->file, pos, SEEK_SET);
    }

    return 0;
}

static int
avi_read_indx(muxed_stream_t *ms)
{
    avi_file_t *af = ms->private;
    int str;
    int idxtype, entries;
    u_char tag[4];

    getu16(af->file);
    url_getc(af->file);
    idxtype = url_getc(af->file);
    entries = getu32(af->file);
    get4c(tag, af->file);

    if(!valid_tag(tag, 0)){
	tc2_print("AVI", TC2_PRINT_WARNING, "Invalid tag in indx chunk.\n");
	return -1;
    }

    str = tag2str(tag);
    if(str > ms->n_streams){
	tc2_print("AVI", TC2_PRINT_WARNING,
		  "Invalid stream #%i in indx chunk.\n", str);
	return -1;
    }

    switch(idxtype){
    case AVI_INDEX_SUPER:
	avi_read_super_indx(ms, str, entries);
	break;

    case AVI_INDEX_CHUNK:
	avi_read_chunk_indx(ms, str, entries);
	break;
    }

    return 0;
}

static void
avi_merge_index(muxed_stream_t *ms)
{
    avi_file_t *af = ms->private;
    int i, j;

    for(i = 0; i < ms->n_streams; i++){
	af->idxlen += af->streams[i].idxlen;
    }

    if(af->idxlen){
	avi_index_t *sx[ms->n_streams];
	for(i = 0; i < ms->n_streams; i++)
	    sx[i] = af->streams[i].index;

	af->index = calloc(af->idxlen, sizeof(*af->index));
	for(i = 0; i < af->idxlen; i++){
	    int x = -1;
	    for(j = 0; j < ms->n_streams; j++){
		if(!sx[j])
		    continue;
		if(!af->index[i])
		    af->index[i] = sx[j];
		if(af->index[i] && sx[j]->offset <= af->index[i]->offset){
		    af->index[i] = sx[j];
		    x = j;
		}
	    }
	    if(x >= 0 &&
	       ++sx[x] - af->streams[x].index == af->streams[x].idxlen){
		sx[x] = NULL;
	    }
	}
    }
}

static muxed_stream_t *
avi_header(url_t *f)
{
    muxed_stream_t *ms = NULL;
    avi_file_t *af;
    uint32_t tag, size, next = 0, stype = 0, sidx = -1;
    uint32_t width = 0, height = 0;
    uint32_t ftime = 0, start = 0;
    char st[5] = {[4] = 0};
    uint64_t fsize, pos;
    int odml_idx = 0;
    stream_t *vs = NULL;

    fsize = f->size;

    if(url_getu32l(f, &tag))
	return NULL;

    if(tag != TAG('R','I','F','F')){
	tc2_print("AVI", TC2_PRINT_ERROR, "no RIFF header\n");
	return NULL;
    }

    getu32(f);			/* size */

    url_getu32l(f, &tag);
    if(tag != TAG('A','V','I',' ')){
	tc2_print("AVI", TC2_PRINT_ERROR, "non-AVI RIFF header\n");
	return NULL;
    }

    ms = tcallocd(sizeof(*ms), NULL, avi_free);
    memset(ms, 0, sizeof(*ms));
    af = calloc(1, sizeof(*af));
    af->file = tcref(f);
    pthread_mutex_init(&af->mx, NULL);
    ms->private = af;
    af->packets = tclist_new(TC_LOCK_NONE);

    while(!url_getu32l(f, &tag)){
	size = getu32(f);
	size += size & 1;
	pos = f->tell(f);

	tc2_print("AVI", TC2_PRINT_DEBUG, "tag '%s', size %li\n",
		  tagstr(st, tag), size);

	if(pos + size > fsize){
	    tc2_print("AVI", TC2_PRINT_WARNING,
		      "Chunk '%s' @ %llx exceeds file size. "
		      "Chunk size %u, remains %llu of file\n",
		      st, pos, size, fsize - pos);
	}

	if(next && tag != next){
	    tc2_print("AVI", TC2_PRINT_ERROR, "error unexpected tag\n");
	    return NULL;
	}

	switch(tag){
	case TAG('L','I','S','T'): {
	    uint32_t lt = getu32(f);
	    if(lt == TAG('m','o','v','i')){
		if(!af->movi_start)
		    af->movi_start = f->tell(f);
		f->seek(f, size-4, SEEK_CUR);
	    } else if(lt == TAG('s','t','r','l')){
		next = TAG('s','t','r','h');
	    }
	    continue;
	}
	case TAG('a','v','i','h'):{
	    ftime = getu32(f);

	    getjunk(f, 32);
	    getjunk(f, 32);
	    getval(f, "flags", 32);
	    getval(f, "frames", 32);
	    getjunk(f, 32);

	    ms->n_streams = getu32(f);
	    ms->streams = calloc(ms->n_streams, sizeof(*ms->streams));
	    af->streams = calloc(ms->n_streams, sizeof(*af->streams));

	    getjunk(f, 32);

	    width = getu32(f);
	    height = getu32(f);

	    break;
	}
	case TAG('s','t','r','h'):{
	    uint32_t length;

	    sidx++;

	    stype = getu32(f);
	    get4c(st, f);	/* tag */
	    getval(f, "flags", 32);
	    getval(f, "prio", 16);
	    getval(f, "lang", 16);
	    getval(f, "init frame", 32);
	    af->streams[sidx].scale = getu32(f);
	    af->streams[sidx].rate = getu32(f);
	    start = getu32(f);
	    length = getu32(f);
	    getval(f, "bufsize", 32);
	    getval(f, "quality", 32);
	    af->streams[sidx].sample_size = getu32(f);

	    if(stype == TAG('v','i','d','s')){
		uint32_t frn, frd;
		ms->streams[sidx].stream_type = STREAM_TYPE_VIDEO;
		frd = af->streams[sidx].scale;
		frn = af->streams[sidx].rate;
		if(frn && frd){
		    ms->streams[sidx].video.frame_rate.num = frn;
		    ms->streams[sidx].video.frame_rate.den = frd;
		} else {
		    ms->streams[sidx].video.frame_rate.num = 1000000;
		    ms->streams[sidx].video.frame_rate.den = ftime;
		}

		af->streams[sidx].ptsn = 27000000LL * frd;
		af->streams[sidx].ptsd = frn;

		ms->streams[sidx].video.frames = length;

		getjunk(f, 16);
		getjunk(f, 16);

		ms->streams[sidx].video.width = getu16(f);
		ms->streams[sidx].video.height = getu16(f);
		if(!ms->streams[sidx].video.width){
		    ms->streams[sidx].video.width = width;
		    ms->streams[sidx].video.height = height;
		}
		vs = &ms->streams[sidx];
		af->has_video = 1;
	    } else if(stype == TAG('a','u','d','s')){
		ms->streams[sidx].stream_type = STREAM_TYPE_AUDIO;
		af->streams[sidx].ptsn = 27000000LL * af->streams[sidx].scale;
		af->streams[sidx].ptsd = af->streams[sidx].rate;
	    }

	    ms->streams[sidx].common.index = sidx;
	    ms->streams[sidx].common.start_time = start * 27;
	    if(start){
		tc2_print("AVI", TC2_PRINT_WARNING, "start = %i\n", start);
	    }

	    next = TAG('s','t','r','f');
	    break;
	}
	case TAG('s','t','r','f'):{
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
		    f->read(ms->streams[sidx].video.codec_data, 1, cds, f);
		}
		break;

	    case TAG('a','u','d','s'):{
		int id = getu16(f);
		int ba;
		ms->streams[sidx].audio.codec = aid2codec(id);
		ms->streams[sidx].audio.channels = getu16(f);
		ms->streams[sidx].audio.sample_rate = getu32(f);
		ms->streams[sidx].audio.bit_rate = getu32(f) * 8;
		ba = getu16(f);
		ms->streams[sidx].audio.block_align = ba;
		af->streams[sidx].block_align = ba;

		if(size > 14){
		    int ss = getu16(f);
		    ms->streams[sidx].audio.sample_size = ss;
		} else {
		    ms->streams[sidx].audio.sample_size = 8;
		}

		if(id == 1 && ms->streams[sidx].audio.sample_size == 8)
		    ms->streams[sidx].audio.codec = "audio/pcm-u8";

		if(size > 16){
		    cds = getu16(f);
		    if(cds > 0){
			if(cds > size - 18)
			    cds = size - 18;
			ms->streams[sidx].audio.codec_data_size = cds;
			ms->streams[sidx].audio.codec_data = malloc(cds);
			f->read(ms->streams[sidx].audio.codec_data, 1, cds, f);
			af->streams[sidx].wavex = 1;
		    }
		}
		break;
	    }
	    default:
		tc2_print("AVI", TC2_PRINT_WARNING, "bad stream type\n");
	    }

	    next = 0;
	    break;
	}
	case TAG('i','d','x','1'):{
	    if(!odml_idx)
		avi_read_idx1(ms, size);
	    break;
	}
	case TAG('i','n','d','x'):{
	    avi_read_indx(ms);
	    odml_idx = 1;
	    break;
	}
	case TAG('R','I','F','F'):{
	    getu32(f);		/* AVIX */
	    break;
	}
	case TAG('d','m','l','h'):{
	    if(vs)
		vs->video.frames = getu32(f);
	    break;
	}
	case TAG('J','U','N','K'):{
	    break;
	}
	default:
	    tc2_print("AVI", TC2_PRINT_WARNING,
		      "unknown tag '%s' @%llx\n", st, pos);
	}
	f->seek(f, pos + size, SEEK_SET);
    }

    avi_merge_index(ms);

    f->seek(f, af->movi_start, SEEK_SET);

    return ms;
}

static void
avi_free_packet(void *p)
{
    avi_packet_t *ap = p;
    free(ap->buf);
}

static avi_packet_t *
avi_alloc_packet(size_t size)
{
    avi_packet_t *pk = tcallocdz(sizeof(*pk), NULL, avi_free_packet);

    pk->buf = malloc(size + 16);
    memset(pk->buf + size, 0, 16);
    pk->data = pk->buf;
    pk->asize = size;

    pk->pk.data = (u_char **) &pk->data;
    pk->pk.sizes = &pk->size;
    pk->pk.sizes[0] = size;
    pk->pk.planes = 1;

    return pk;
}

static char *
strtag(u_char *tag, u_char *str)
{
    int i;
    for(i = 0; i < 4; i++){
	str[i] = isprint(tag[i])? tag[i]: '.';
    }
    str[4] = 0;
    return str;
}

static tcvp_packet_t *
avi_packet(muxed_stream_t *ms, int stream)
{
    u_char tag[5] = {[4] = 0}, stag[5];
    uint32_t size;
    avi_file_t *af = ms->private;
    int str;
    avi_packet_t *pk;

    int tried_index = 0, tried_bkup = 0, skipped = 0, scan = 0;
    uint32_t flags = 0;
    uint64_t pts = 0;
    int pflags = 0;
    uint64_t pos;

    if(stream > -2 && (pk = tclist_shift(af->packets)))
	return (tcvp_packet_t *) pk;

    /* FIXME: get rid of gotos */
 again:
    pos = af->file->tell(af->file);
    if(!get4c(tag, af->file))
	return NULL;

    if(!valid_tag(tag, scan)){
	if(!strcmp(tag, "RIFF")){
	    af->file->seek(af->file, 8, SEEK_CUR);
	    goto again;
	} else if(!strcmp(tag, "LIST")){
	    int ls = getu32(af->file); /* size */
	    get4c(tag, af->file);
	    if(strcmp(tag, "rec ") && strcmp(tag, "movi"))
		af->file->seek(af->file, ls - 4, SEEK_CUR);
	    goto again;
	} else if(!strcmp(tag, "JUNK") ||
		  !strcmp(tag, "idx1") ||
		  !strncmp(tag, "ix", 2)){
	    size = getu32(af->file);
	    af->file->seek(af->file, size, SEEK_CUR);
	    goto again;
	}

	if(!scan)
	    tc2_print("AVI", TC2_PRINT_WARNING,
		      "Bad chunk tag %02x%02x%02x%02x:%s @ %08llx\n",
		      tag[0], tag[1], tag[2], tag[3],
		      strtag(tag, stag), pos);
	if(!tried_index && af->idxok > 256 && af->idxlen > af->pkt){
	    uint64_t p = af->index[af->pkt]->offset;
	    tc2_print("AVI", TC2_PRINT_DEBUG, "Index => %16llx\n", p);
	    af->file->seek(af->file, p, SEEK_SET);
	    tried_index++;
	    goto again;
	} else if(!tried_bkup){
	    tc2_print("AVI", TC2_PRINT_DEBUG,
		      "Backing up %i bytes.\n", backup);
	    af->file->seek(af->file, -backup - 4, SEEK_CUR);
	    tried_bkup++;
	    goto again;
	} else if(skipped < max_skip && af->idxok > 256 &&
		  af->idxlen > af->pkt){
	    if(!skipped)
		tc2_print("AVI", TC2_PRINT_WARNING, "Skipping chunk.\n");
	    af->file->seek(af->file, af->index[af->pkt]->offset, SEEK_SET);
	    af->pkt++;
	    skipped++;
	    goto again;
	} else if(scan < max_scan){
	    af->file->seek(af->file, -3, SEEK_CUR);
	    scan++;
	    goto again;
	}

	tc2_print("AVI", TC2_PRINT_ERROR, "Can't find valid chunk tag.\n");
	af->eof = 1;
	return NULL;
    }

    if(skipped)
	tc2_print("AVI", TC2_PRINT_WARNING, "Skipped %i chunks\n", skipped);

    if(scan)
	tc2_print("AVI", TC2_PRINT_DEBUG, "Resuming @%08llx\n", pos);

    size = getu32(af->file);
    str = tag2str(tag);

    if(size > 1 << 24)
	goto again;

    if(!(size & ~0x12)){
	char tag[4];
	af->file->seek(af->file, 8, SEEK_CUR);
	if(!get4c(tag, af->file))
	    return NULL;
	af->file->seek(af->file, -12, SEEK_CUR);
	if(valid_tag(tag, 0) || !memcmp(tag, "rec ", 4)){
	    tried_bkup++;
	    goto again;
	} else if(!memcmp(tag, "LIST", 4)){
	    af->file->seek(af->file, 8, SEEK_CUR);
	    goto again;
	}
	scan++;
    }

    if(af->index){
	int i;

	for(i = af->pkt; i < af->idxlen; i++){
	    if(pos <= af->index[i]->offset){
		af->pkt = i;
		break;
	    }
	}

	if(i != af->pkt){
	    tc2_print("AVI", TC2_PRINT_WARNING, "Can't resync index.\n");
	    af->idxok = 0;
	}
    }

    if(str >= ms->n_streams){
	tc2_print("AVI", TC2_PRINT_WARNING, "Bad stream number %i @%08llx\n",
		  str, pos);
	scan++;
	goto again;
    }

    if(!size || !ms->used_streams[str]){
	af->file->seek(af->file, size + (size&1), SEEK_CUR);
	tried_index = 0;
	tried_bkup = 0;
	scan = 0;
	skipped = 0;
	af->pkt++;
	goto again;
    }

    if(af->pkt < af->idxlen){
	if(af->index[af->pkt]->offset == pos){
	    af->idxok++;
	    flags = af->index[af->pkt]->flags;
	    if(flags & AVI_FLAG_KEYFRAME)
		pflags |= TCVP_PKT_FLAG_KEY;
	    pts = af->index[af->pkt]->pts + starttime;
	    pflags |= TCVP_PKT_FLAG_PTS;
	} else {
	    af->idxok = 0;
	}
    }

    pk = avi_alloc_packet(size);
    af->file->read(pk->data, 1, size, af->file);
    if(size & 1)
	url_getc(af->file);

    pk->pk.stream = str;
    pk->pk.flags = pflags;
    pk->pk.pts = pts;
    pk->flags = flags;

    af->pkt++;

    return (tcvp_packet_t *) pk;
}

static uint64_t
avi_seek(muxed_stream_t *ms, uint64_t time)
{
    avi_file_t *af = ms->private;
    uint64_t pos = -1;
    avi_packet_t *pk;
    int fi[ms->n_streams];
    int i, cfi = 0;

    for(i = 0; i < af->idxlen; i++){
	if(time <= af->index[i]->pts){
	    if(af->index[i]->offset < pos)
		pos = af->index[i]->offset;
	    if(!af->has_video)
		break;
	    if(af->index[i]->flags & AVI_FLAG_KEYFRAME){
		time = af->index[i]->pts;
		break;
	    }
	}
    }

    if(pos == -1)
	return -1LL;

    for(i = 0; i < af->idxlen; i++){
	if(af->index[i]->offset >= pos){
	    af->pkt = i;
	    break;
	}
    }

    while((pk = tclist_shift(af->packets)))
	avi_free_packet(&pk->pk);

    af->file->seek(af->file, pos, SEEK_SET);

    for(i = 0; i < ms->n_streams; i++){
	if(ms->used_streams[i])
	    cfi++;
	fi[i] = 0;
    }

    while(cfi){
	avi_packet_t *pk = NULL;
	int s;

	if(!(pk = (avi_packet_t *) avi_packet(ms, -2)))
	    break;

	s = pk->pk.stream;

	if(time <= pk->pk.pts){
	    if(!fi[s]){
		fi[s] = 1;
		cfi--;
	    }
	}

	if(fi[s]){
	    tclist_push(af->packets, pk);
	} else {
	    avi_free_packet(&pk->pk);
	}
    }

    return time;
}

static tcvp_packet_t *
avi_packet_ni(muxed_stream_t *ms, int str)
{
    avi_file_t *af = ms->private;
    avi_stream_t *as = af->streams + str;
    avi_packet_t *pk = NULL;

    if(str < 0)
	return avi_packet(ms, str);

    if(as->pkt < as->idxlen){
	avi_index_t *ai = as->index + as->pkt++;

	pk = avi_alloc_packet(ai->size);
	af->file->seek(af->file, ai->offset + 8, SEEK_SET);
	af->file->read(pk->data, 1, ai->size, af->file);
	pk->pk.stream = str;
	pk->pk.flags = TCVP_PKT_FLAG_PTS;
	if(ai->flags & AVI_FLAG_KEYFRAME)
	    pk->pk.flags |= TCVP_PKT_FLAG_KEY;
	pk->pk.pts = ai->pts;
	pk->flags = ai->flags;
    }

    return (tcvp_packet_t *) pk;
}

static uint64_t
avi_seek_ni(muxed_stream_t *ms, uint64_t time)
{
    avi_file_t *af = ms->private;
    int i, j;

    for(j = 0; j < ms->n_streams; j++){
	if(ms->used_streams[j]){
	    for(i = 0; i < af->streams[j].idxlen; i++){
		if(time <= af->streams[j].index[i].pts){
		    if(ms->streams[j].stream_type != STREAM_TYPE_VIDEO ||
		       (af->streams[j].index[i].flags & AVI_FLAG_KEYFRAME))
			break;
		}
	    }
	    if(ms->streams[j].stream_type == STREAM_TYPE_VIDEO)
		time = af->streams[j].index[i].pts;
	}
    }

    for(j = 0; j < ms->n_streams; j++){
	if(ms->used_streams[j]){
	    for(i = 0; i < af->streams[j].idxlen; i++){
		if(time <= af->streams[j].index[i].pts){
		    af->streams[j].pkt = i;
		    break;
		}
	    }
	}
    }

    return time;
}

static void
avi_free(void *p)
{
    muxed_stream_t *ms = p;
    avi_file_t *af = ms->private;
    int i;

    free(ms->streams);
    free(ms->used_streams);

    tclist_destroy(af->packets, (tcfree_fn) avi_free_packet);

    for(i = 0; i < ms->n_streams; i++){
	if(af->streams[i].index){
	    free(af->streams[i].index);
	}
    }

    if(af->index)
	free(af->index);

    af->file->close(af->file);
    free(af);
}

extern muxed_stream_t *
avi_open(char *file, url_t *f, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;

    if(!(ms = avi_header(f)))
	return NULL;

    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));
    if(tcvp_demux_avi_conf_noninterleaved){
	ms->next_packet = avi_packet_ni;
	ms->seek = avi_seek_ni;
    } else {
	ms->next_packet = avi_packet;
	ms->seek = avi_seek;
    }

    return ms;
}

static char *
vtag2codec(char *tag)
{
    int i;

    for(i = 0; i < tcvp_demux_avi_conf_vtag_count; i++){
	if(!memcmp(tag, tcvp_demux_avi_conf_vtag[i].tag, 4))
	    return tcvp_demux_avi_conf_vtag[i].codec;
    }

    tc2_print("AVI", TC2_PRINT_WARNING, "unknown codec tag '%s'\n", tag);
    return NULL;
}

static char *
aid2codec(int id)
{
    int i;

    for(i = 0; i < tcvp_demux_avi_conf_atag_count; i++){
	if(id == tcvp_demux_avi_conf_atag[i].tag)
	    return tcvp_demux_avi_conf_atag[i].codec;
    }

    tc2_print("AVI", TC2_PRINT_WARNING, "unknown codec ID %#x\n", id);
    return NULL;
}

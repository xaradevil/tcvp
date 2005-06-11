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
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <mp3_tc2.h>
#include "id3.h"
#include "mp3.h"

#define MAX_FRAME_SIZE 8065
#define MAX_HEADER_SIZE 6

typedef struct mp3_file {
    url_t* file;
    stream_t stream;
    int used;
    uint64_t start;
    uint64_t size;
    eventq_t qs;
    uint64_t sbr;
    size_t bytes;
    uint64_t samples;
    int header_size;
    u_char *buf;
    int bufsize;
    int bhead, btail;
    int (*parse_header)(u_char *, mp3_frame_t *);
    int xtime;
    u_char *xing;
    char *tag;
} mp3_file_t;

static char *codecs[] = {
    "audio/mp1",
    "audio/mp2",
    "audio/mp3",
    "audio/aac",
    "audio/ac3"
};

struct header_parser {
    int (*parser)(u_char *, mp3_frame_t *);
    int header_size;
    char *tag;
} header_parsers[] = {
    { mp3_header, 3, "MP3" },
    { aac_header, 6, "AAC" },
    { ac3_header, 5, "AC3" },
};

#define NUM_PARSERS (sizeof(header_parsers) / sizeof(header_parsers[0]))

static int
all_headers(mp3_file_t *mf, u_char *head, mp3_frame_t *fr)
{
    int i;

    if(mf->parse_header)
	return mf->parse_header(head, fr);

    for(i = 0; i < NUM_PARSERS; i++){
	if(!header_parsers[i].parser(head, fr)){
	    mf->parse_header = header_parsers[i].parser;
	    mf->header_size = header_parsers[i].header_size;
	    mf->tag = header_parsers[i].tag;
	    return 0;
	}
    }

    return -1;
}

static int
mp3_getparams(muxed_stream_t *ms)
{
    mp3_file_t *mf = ms->private;
    url_t *f = mf->file;
    mp3_frame_t fr;
    uint64_t maxscan, i;
    off_t pos = f->tell(f);
    u_char head[MAX_HEADER_SIZE];
    int found = 0;
    int resync;

    if(mf->parse_header){
	resync = 1;
	maxscan = -1LL;
    } else {
	resync = 0;
	maxscan = MAX_FRAME_SIZE;
    }

    if(f->read(head, 1, MAX_HEADER_SIZE, f) < MAX_HEADER_SIZE)
	return -1;

    for(i = 0; i < maxscan; i++){
	if(!all_headers(mf, head, &fr)){
	    off_t pos2 = f->tell(f);
	    u_char head2[MAX_HEADER_SIZE];
	    f->seek(f, pos + i + fr.size, SEEK_SET);
	    if(f->read(head2, 1, mf->header_size, f) < mf->header_size)
		break;
	    if(!mf->parse_header(head2, &fr)){
		f->seek(f, pos + i, SEEK_SET);
		found = 1;
		break;
	    }
	    f->seek(f, pos2, SEEK_SET);
	    if(!resync)
		mf->parse_header = NULL;
	}

	memmove(head, head + 1, MAX_HEADER_SIZE - 1);
	if(f->read(head + MAX_HEADER_SIZE - 1, 1, 1, f) != 1)
	    return -1;
    }

    if(!found)
	return -1;

    if(!mf->stream.audio.bit_rate)
	mf->stream.audio.bit_rate = fr.bitrate;
    mf->stream.audio.codec = codecs[fr.layer];
    mf->stream.audio.sample_rate = fr.sample_rate;
    if(fr.bitrate && !ms->time)
	ms->time = 27 * 8000000LL * mf->size / fr.bitrate;

    return 0;
}

static uint64_t
mp3_seek(muxed_stream_t *ms, uint64_t time)
{
    mp3_file_t *mf = ms->private;
    uint64_t pos;

    if(!mf->stream.audio.bit_rate)
	return -1LL;

    if(mf->xing){
	int xi = 100 * time / ms->time;
	pos = mf->xing[xi] * mf->size / 256;
	time = ms->time * xi / 100;
    } else {
	pos = time * mf->stream.audio.bit_rate / (27 * 8000000);
    }

    if(pos > mf->size)
	return -1LL;

    mf->file->seek(mf->file, mf->start + pos, SEEK_SET);
    if(!mp3_getparams(ms))
	if(mf->stream.audio.bit_rate && !mf->xtime)
	    time = pos * 27 * 8000000LL / mf->stream.audio.bit_rate;

    mf->samples = time / 27000000 * mf->stream.audio.sample_rate;
    mf->bhead = 0;
    mf->btail = 0;
    tcfree(mf->buf);
    mf->buf = tcalloc(mf->bufsize);

    return time;
}

typedef struct mp3_packet {
    tcvp_data_packet_t pk;
    u_char *data;
    u_char *buf;
    int size;
} mp3_packet_t;

static void
mp3_free_pk(void *p)
{
    mp3_packet_t *mp = p;
    tcfree(mp->buf);
}

static int
fill_buffer(mp3_file_t *mf)
{
    u_char *data = mf->buf + mf->bhead;
    int size = mf->bufsize - mf->bhead;
    uint64_t pos;

    pos = mf->file->tell(mf->file);
    size = min(size, mf->size - pos + mf->start);
    size = mf->file->read(data, 1, size, mf->file);

    if(size > 0)
	mf->bhead += size;

    return size;
}

static mp3_packet_t *
make_packet(mp3_file_t *mf, int offset, mp3_frame_t *fr)
{
    mp3_packet_t *mp;

    mp = tcallocdz(sizeof(*mp), NULL, mp3_free_pk);
    mp->data = mf->buf + offset;
    mp->buf = tcref(mf->buf);
    mp->size = fr->size;
    mp->pk.stream = 0;
    mp->pk.data = &mp->data;
    mp->pk.sizes = &mp->size;
    mp->pk.planes = 1;
    mp->pk.flags = TCVP_PKT_FLAG_PTS;
    mp->pk.pts = mf->samples * 27000000LL / mf->stream.audio.sample_rate;
    mp->pk.samples = fr->samples;

    return mp;
}

static tcvp_packet_t *
mp3_packet(muxed_stream_t *ms, int str)
{
    mp3_file_t *mf = ms->private;
    mp3_packet_t *mp = NULL;
    mp3_frame_t fr;
    int size;
    u_char *nb;
    int eof = 0;

    if(!mf->used)
	return NULL;

    while(!mp){
	if(mf->bhead < mf->bufsize){
	    if(fill_buffer(mf) <= 0)
		eof = 1;
	}

	fr.size = 0;
	while(mf->bhead - mf->btail >= mf->header_size){
	    if(!mf->parse_header(mf->buf + mf->btail, &fr)){
		u_int br;

		if(fr.size > mf->bhead - mf->btail)
		    break;

		mp = make_packet(mf, mf->btail, &fr);

		mf->samples += fr.samples;
		mf->sbr += (uint64_t) fr.size * fr.bitrate;
		mf->bytes += fr.size;
		br = mf->sbr / mf->bytes;
		if(br != mf->stream.audio.bit_rate){
		    mf->stream.audio.bit_rate = br;
		    if(!mf->xtime){
			ms->time = 27 * 8000000LL * mf->size / br;
			if(mf->qs)
			    tcvp_event_send(mf->qs, TCVP_STREAM_INFO);
		    }
		    tc2_print(mf->tag, TC2_PRINT_DEBUG+1,
			      "bitrate %i [%u] %lli s @%llx\n",
			      fr.bitrate, br, ms->time / 27000000,
			      mf->file->tell(mf->file) -
			      (mf->bhead - mf->btail));
		}
		mf->btail += fr.size;
		break;
	    } else {
		uint64_t pos1, pos2;
		u_char *h = mf->buf + mf->btail;
		pos1 = mf->file->tell(mf->file) - (mf->bhead - mf->btail);
		tc2_print(mf->tag, TC2_PRINT_WARNING,
			  "bad header %02x%02x%02x @ %llx\n",
			  h[0], h[1], h[2], pos1);
		mf->bhead = 0;
		mf->btail = 0;
		mf->file->seek(mf->file, pos1, SEEK_SET);
		if(mp3_getparams(ms) < 0){
		    eof = 1;
		    break;
		}
		pos2 = mf->file->tell(mf->file);
		tc2_print(mf->tag, TC2_PRINT_WARNING,
			  "sync at %llx, skipped %i bytes\n",
			  pos2, pos2 - pos1);
	    }
	}

	size = mf->bhead - mf->btail;
	if(size < fr.size || size < mf->header_size){
	    if(eof && !mp)
		return NULL;
	    nb = tcalloc(mf->bufsize);
	    memcpy(nb, mf->buf + mf->btail, mf->bhead - mf->btail);
	    tcfree(mf->buf);
	    mf->buf = nb;
	    mf->bhead = size;
	    mf->btail = 0;
	}
    }

    return (tcvp_packet_t *) mp;
}

static void
mp3_free(void *p)
{
    muxed_stream_t *ms = p;
    mp3_file_t *mf = ms->private;

    eventq_delete(mf->qs);
    if(mf->file)
	mf->file->close(mf->file);
    tcfree(mf->buf);
    free(mf->xing);
    free(mf);
}

#define XING_SIZE 512

static int
xing_header(muxed_stream_t *ms)
{
    mp3_file_t *mf = ms->private;
    u_char x[XING_SIZE], *xp;
    int i, flags;
    uint64_t fp = mf->file->tell(mf->file);
    mp3_frame_t fr;
    int size;

    if(mf->file->read(x, 1, XING_SIZE, mf->file) < XING_SIZE)
	return -1;

    mf->file->seek(mf->file, fp, SEEK_SET);

    if(mf->parse_header(x, &fr))
	return -1;

    size = min(fr.size, XING_SIZE) - 4;

    for(i = 0; i < size; i++)
	if(!strncmp(x + i, "Xing", 4))
	    break;

    if(i == size)
	return -1;

    xp = x + i + 4;
    flags = xp[3];
    xp += 4;

    if(flags & 0x1){
	int frames = htob_32(unaligned32(xp));
	uint64_t samples = 1152 * frames;
	ms->time = 27000000LL * samples / mf->stream.audio.sample_rate;
	mf->stream.audio.samples = samples;
	mf->stream.audio.bit_rate =
	    mf->size * 8 * mf->stream.audio.sample_rate / samples;
	mf->xtime = 1;
	xp += 4;
    }

    if(flags & 0x2)
	xp += 4;

    if(flags & 0x4){
	mf->xing = malloc(100);
	memcpy(mf->xing, xp, 100);
    }

    return 0;
}

extern muxed_stream_t *
mp3_open(char *name, url_t *f, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;
    mp3_file_t *mf;
    char *qname, *qn;
    u_char head[4];
    int ts = 0;

    ms = tcallocdz(sizeof(*ms), NULL, mp3_free);
    mf = calloc(1, sizeof(*mf));

    ms->n_streams = 1;
    ms->streams = &mf->stream;
    ms->private = mf;

    mf->file = tcref(f);
    mf->stream.stream_type = STREAM_TYPE_AUDIO;
    mf->size = f->size;

    id3v2_tag(f, ms);
    if(!f->flags & URL_FLAG_STREAMED)
	ts = id3v1_tag(f, ms);

    if(ts > 0)
	mf->size -= ts;

    f->read(head, 1, 4, f);
    if(strncmp(head, "RIFF", 4)){
	f->seek(f, -4, SEEK_CUR);
    } else {
	f->seek(f, 44, SEEK_SET);
    }

    tc2_print("MP3", TC2_PRINT_DEBUG, "data start %x\n", f->tell(f));

    mf->header_size = MAX_HEADER_SIZE;

    if(mp3_getparams(ms)){
	tcfree(ms);
	return NULL;
    }

    mf->samples = (uint64_t) tcvp_demux_mp3_conf_starttime *
	mf->stream.audio.sample_rate / 1000;

    ms->next_packet = mp3_packet;
    ms->seek = mp3_seek;
    ms->used_streams = &mf->used;

    mf->start = f->tell(f);
    mf->size -= mf->start;

    tc2_print(mf->tag, TC2_PRINT_DEBUG, "data start %x\n", f->tell(f));

    xing_header(ms);

    mf->bufsize = MAX_FRAME_SIZE;
    mf->buf = tcalloc(mf->bufsize);

    if(tcconf_getvalue(cs, "qname", "%s", &qname) > 0){
	qn = alloca(strlen(qname) + 8);
	mf->qs = eventq_new(NULL);
	sprintf(qn, "%s/status", qname);
	eventq_attach(mf->qs, qn, EVENTQ_SEND);
	free(qname);
    }

    return ms;
}

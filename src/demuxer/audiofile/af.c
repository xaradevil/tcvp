/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tcalloc.h>
#include <audiofile_tc2.h>
#include <audiofile.h>
#include <af_vfs.h>

#define PK_SIZE tcvp_demux_audiofile_conf_packet_size

char *formats[] = {
    "audio/pcm-u8",
    "audio/pcm-s8",
    "audio/pcm-u16" TCVP_ENDIAN,
    "audio/pcm-s16" TCVP_ENDIAN,
};

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static ssize_t
aff_read(AFvirtualfile *vfile, void *data, size_t nbytes)
{
    url_t *f = vfile->closure;
    return f->read(data, 1, nbytes, f);
}

static long
aff_length(AFvirtualfile *vfile)
{
    long curpos, retval;
    url_t *f = vfile->closure;

    curpos = f->tell(f);
    f->seek(f, 0, SEEK_END);
    retval = f->tell(f);
    f->seek(f, curpos, SEEK_SET);

    return retval;
}

static void
aff_destroy(AFvirtualfile *vfile)
{
    url_t *f = vfile->closure;
    f->close(f);
    vfile->closure = NULL;
}

static long
aff_seek(AFvirtualfile *vfile, long offset, int is_relative)
{
    url_t *f = vfile->closure;
    f->seek(f, offset, is_relative?SEEK_CUR:SEEK_SET);

    return f->tell(f);
}

static long
aff_tell(AFvirtualfile *vfile)
{
    url_t *f = vfile->closure;
    return f->tell(f);
}


static void
af_free_packet(void *v)
{
    packet_t *p = v;
    free(p->data[0]);
    free(p->data);
    free(p->sizes);
}

extern packet_t *
af_next_packet(muxed_stream_t *ms, int stream)
{
    AFfilehandle aff = ms->private;
    packet_t *pk;
    int i;
    float fs = afGetFrameSize(aff, AF_DEFAULT_TRACK, 0);

    pk = tcallocd(sizeof(*pk), NULL, af_free_packet);
    pk->stream = 0;
    pk->flags = 0;
    pk->data = malloc(sizeof(*pk->data));
    pk->data[0] = malloc(PK_SIZE);

    pthread_mutex_lock(&lock);
    i = afReadFrames (aff, AF_DEFAULT_TRACK, pk->data[0], PK_SIZE/fs);
    pthread_mutex_unlock(&lock);
    if(i == 0) {
	return NULL;
    }

    pk->sizes = malloc(sizeof(*pk->sizes));
    pk->sizes[0] = i*fs;
    pk->planes = 1;

    return pk;
}

static uint64_t
af_seek(muxed_stream_t *ms, uint64_t time)
{
    AFfilehandle aff = ms->private;
    int sr = ms->streams[0].audio.sample_rate;
    uint64_t frames;

    pthread_mutex_lock(&lock);
    frames = afSeekFrame(aff, AF_DEFAULT_TRACK, time*sr/27000000);
    pthread_mutex_unlock(&lock);

    return frames*27000000/sr;
}

static void
af_free(void *p)
{
    muxed_stream_t *ms = p;
    AFfilehandle aff = ms->private;

    afCloseFile(aff);
}

extern muxed_stream_t *
af_open(char *name, url_t *f, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;
    AFfilehandle aff;
    AFvirtualfile *vf;
    int sampleFormat, sampleWidth, byteOrder, fn=0;

    vf = af_virtual_file_new();
    vf->closure = tcref(f);
    vf->read = aff_read;
    vf->length = aff_length;
    vf->destroy = aff_destroy;
    vf->seek = aff_seek;
    vf->tell = aff_tell;

    aff = afOpenVirtualFile(vf, "r", AF_NULL_FILESETUP);
    if(!aff){
	af_virtual_file_destroy(vf);
	return NULL;
    }

    afGetSampleFormat(aff, AF_DEFAULT_TRACK, &sampleFormat, &sampleWidth);
    byteOrder = afGetByteOrder(aff, AF_DEFAULT_TRACK);
/*     fn += (byteOrder == AF_BYTEORDER_BIGENDIAN)?1:0; */
    fn += (sampleFormat == AF_SAMPFMT_TWOSCOMP);
    fn += (sampleWidth == 16) << 1;

    ms = tcallocd(sizeof(*ms), NULL, af_free);
    memset(ms, 0, sizeof(*ms));
    ms->n_streams = 1;
    ms->streams = calloc(1, sizeof(*ms->streams));

    ms->streams[0].stream_type = STREAM_TYPE_AUDIO;
    ms->streams[0].audio.codec = formats[fn];
    ms->streams[0].audio.sample_rate = afGetRate(aff, AF_DEFAULT_TRACK);
    ms->streams[0].audio.channels = afGetChannels(aff, AF_DEFAULT_TRACK);
    ms->streams[0].audio.samples = afGetFrameCount(aff, AF_DEFAULT_TRACK);
    ms->streams[0].audio.bit_rate = afGetRate(aff, AF_DEFAULT_TRACK) *
	afGetFrameSize(aff, AF_DEFAULT_TRACK, 0) * 8;

    ms->used_streams = calloc(ms->n_streams, sizeof(*ms->used_streams));
    ms->next_packet = af_next_packet;
    ms->seek = af_seek;
    ms->private = aff;

    return ms;
}

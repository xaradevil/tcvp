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
#include <tcalloc.h>
#include <tcendian.h>
#include <tcvp_types.h>
#include <FLAC/stream_decoder.h>
#include <flac_tc2.h>

typedef struct flac_decode {
    FLAC__StreamDecoder *fsd;
    u_char *data;
    int size;
    int frame;
} flac_decode_t;

typedef struct flac_decode_packet {
    tcvp_data_packet_t pk;
    u_char *data;
    int size;
} flac_decode_packet_t;

static void
flac_free_pk(void *p)
{
    flac_decode_packet_t *fp = p;
    free(fp->data);
}

static FLAC__StreamDecoderReadStatus
flac_read(const FLAC__StreamDecoder *fsd, FLAC__byte buf[], unsigned *rb,
	  void *d)
{
    tcvp_pipe_t *p = d;
    flac_decode_t *fd = p->private;

    tc2_print("FLACDEC", TC2_PRINT_DEBUG, "flac_read(%i) = %i state %s\n",
	      *rb, fd->size,
	      FLAC__stream_decoder_get_resolved_state_string(fsd));

    if(fd->size <= 0)
	return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;

    if(*rb > fd->size)
	*rb = fd->size;

    memcpy(buf, fd->data, *rb);
    fd->data += *rb;
    fd->size -= *rb;

    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderWriteStatus
flac_write(const FLAC__StreamDecoder *fsd, const FLAC__Frame *fr,
	   const FLAC__int32 *const buf[], void *d)
{
    tcvp_pipe_t *p = d;
    flac_decode_packet_t *fp;
    int samples = fr->header.blocksize;
    int16_t *out;
    int i, j;

    tc2_print("FLACDEC", TC2_PRINT_DEBUG, "flac_write()\n");

    fp = tcallocdz(sizeof(*fp), NULL, flac_free_pk);
    fp->size = sizeof(*out) * fr->header.channels * samples;
    out = malloc(fp->size);
    fp->data = (u_char *) out;

    for(i = 0; i < samples; i++)
	for(j = 0; j < fr->header.channels; j++)
	    *out++ = buf[j][i];

    fp->pk.stream = p->format.common.index;
    fp->pk.data = &fp->data;
    fp->pk.sizes = &fp->size;
    fp->pk.samples = samples;
    
    p->next->input(p->next, (tcvp_packet_t *) fp);

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void
flac_metadata(const FLAC__StreamDecoder *fsd, const FLAC__StreamMetadata *md,
	      void *p)
{
    tc2_print("FLACDEC", TC2_PRINT_DEBUG, "flac_metadata()\n");
}

static void
flac_error(const FLAC__StreamDecoder *fsd, FLAC__StreamDecoderErrorStatus st,
	   void *p)
{
    tc2_print("FLACDEC", TC2_PRINT_ERROR, "error %i, state %s\n",
	      st, FLAC__stream_decoder_get_resolved_state_string(fsd));
}

extern int
flacdec_decode(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    flac_decode_t *fd = p->private;
    int n = 0;

    tc2_print("FLACDEC", TC2_PRINT_DEBUG, "flacdec_decode()\n");

    if(!pk->data){
	p->next->input(p->next, (tcvp_packet_t *) pk);
	return 0;
    }

    fd->data = pk->data[0];
    fd->size = pk->sizes[0];

    while(fd->size > 0){
	FLAC__stream_decoder_process_single(fd->fsd);
	n++;
    }

    if(n > 1)
	tc2_print("FLACDEC", TC2_PRINT_WARNING,
		  "%i frames in packet at frame %i\n", n, fd->frame);
    fd->frame += n;

    tcfree(pk);

    return 0;
}

extern int
flacdec_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    flac_decode_t *fd = p->private;
    int srate, channels;
    u_char *buf;

    tc2_print("FLACDEC", TC2_PRINT_DEBUG, "flacdec_probe()\n");

    fd->size = s->common.codec_data_size + 8;
    buf = malloc(fd->size);
    fd->data = buf;
    memcpy(fd->data, "fLaC", 4);
    *(uint32_t *) (fd->data + 4) =
	htob_32((0x80 << 24) + s->common.codec_data_size);
    memcpy(fd->data + 8, s->common.codec_data, s->common.codec_data_size);

    FLAC__stream_decoder_process_single(fd->fsd);
    srate = FLAC__stream_decoder_get_sample_rate(fd->fsd);
    channels = FLAC__stream_decoder_get_channels(fd->fsd);

    free(buf);

    p->format.stream_type = STREAM_TYPE_AUDIO;
    p->format.common.codec = "audio/pcm-s16" TCVP_ENDIAN;
/*     p->format.audio.sample_rate = srate; */
/*     p->format.audio.channels = channels; */
    p->format.audio.bit_rate = srate * channels * 16;

    tcfree(pk);

    tc2_print("FLACDEC", TC2_PRINT_DEBUG, "flacdec_probe() OK\n");

    return PROBE_OK;
}

static void
flac_free_codec(void *p)
{
    flac_decode_t *fd = p;
    FLAC__stream_decoder_delete(fd->fsd);
}

extern int
flacdec_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,
	    tcvp_timer_t *t, muxed_stream_t *ms)
{
    flac_decode_t *fd;
    int status;

    fd = tcallocdz(sizeof(*fd), NULL, flac_free_codec);
    fd->fsd = FLAC__stream_decoder_new();
    FLAC__stream_decoder_set_read_callback(fd->fsd, flac_read);
    FLAC__stream_decoder_set_write_callback(fd->fsd, flac_write);
    FLAC__stream_decoder_set_metadata_callback(fd->fsd, flac_metadata);
    FLAC__stream_decoder_set_error_callback(fd->fsd, flac_error);
    FLAC__stream_decoder_set_client_data(fd->fsd, p);

    status = FLAC__stream_decoder_init(fd->fsd);
    tc2_print("FLACDEC", TC2_PRINT_DEBUG, "state %s\n",
	      FLAC__stream_decoder_get_resolved_state_string(fd->fsd));

    p->private = fd;

    return 0;
}

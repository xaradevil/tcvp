/**
    Copyright (C) 2004  Michael Ahlberg, Måns Rullgård

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
#include <tctypes.h>
#include <tcendian.h>
#include <tcvp_types.h>
#include <dvdsub_tc2.h>

typedef struct dvdsub_image {
    u_char *even;
    u_char *odd;
    int start, end;
    int x, y;
    int width, height;
    int palette[4];
    u_char alpha[4];
} dvdsub_image_t;

typedef struct dvdsub {
    u_char *buf;
    size_t psize, bpos, bsize;
    uint64_t pts;
    uint32_t *palette;
} dvdsub_t;

typedef struct dvdsub_packet {
    tcvp_data_packet_t pk;
    u_char *data;
    int size;
} dvdsub_packet_t;

static int
dvdsub_info(dvdsub_image_t *dsi, u_char *sdata, int size)
{
    u_char *data = sdata;
    u_int dsize = htob_16(unaligned16(data + 2));
    int x1, x2, y1, y2;
    int rle1, rle2;
    int last = 0;

    dsi->start = -1;
    dsi->end = -1;

    tc2_print("DVDSUB", TC2_PRINT_DEBUG, "dsize %x\n", dsize);

    data += dsize;
    size -= dsize;

    while(size > 0 && !last){
	int date = htob_16(unaligned16(data));
	int next = htob_16(unaligned16(data + 2));
	last = next == data - sdata;

	data += 4;
	size -= 4;

	while(*data != 0xff){
	    int cmd = *data++;
	    size--;

	    tc2_print("DVDSUB", TC2_PRINT_DEBUG+1, "cmd %x\n", cmd);

	    switch(cmd){
	    case 0:
		break;
	    case 1:
		dsi->start = date;
		break;
	    case 2:
		dsi->end = date;
		break;
	    case 3:
		dsi->palette[3] = *data >> 4;
		dsi->palette[2] = *data++ & 0xf;
		dsi->palette[1] = *data >> 4;
		dsi->palette[0] = *data++ & 0xf;
		size -= 2;
		break;
	    case 4:
		dsi->alpha[3] = *data >> 4;
		dsi->alpha[2] = *data++ & 0xf;
		dsi->alpha[1] = *data >> 4;
		dsi->alpha[0] = *data++ & 0xf;
		size -= 2;
		break;
	    case 5:
		x1 = *data++ << 4;
		x1 += *data >> 4;
		x2 = (*data++ & 0xf) << 8;
		x2 += *data++;

		y1 = *data++ << 4;
		y1 += *data >> 4;
		y2 = (*data++ & 0xf) << 8;
		y2 += *data++;

		size -= 6;

		dsi->x = x1;
		dsi->y = y1;
		dsi->width = x2 - x1 + 1;
		dsi->height = y2 - y1 + 1;
		break;
	    case 6:
		rle1 = htob_16(unaligned16(data));
		rle2 = htob_16(unaligned16(data + 2));
		data += 4;
		size -= 4;

		dsi->even = sdata + rle1;
		dsi->odd = sdata + rle2;
		break;
	    default:
		tc2_print("DVDSUB", TC2_PRINT_WARNING,
			  "unknown cmd %x\n", cmd);
		break;
	    }
	}

	data++;
	size--;
    }

    return 0;
}

static inline int
getnibble(u_char **d, int *s)
{
    int v;

    if(*s){
	*s = 0;
	v = **d & 0xf;
	(*d)++;
    } else {
	*s = 1;
	v = **d >> 4;
    }

    return v;
}

static inline int
getval(u_char **d, int *s)
{
    int v = getnibble(d, s);

    if(v > 3)
	return v;
    v <<= 4;
    v += getnibble(d, s);
    if(v > 0xf)
	return v;
    v <<= 4;
    v += getnibble(d, s);
    if(v > 0x3f)
	return v;
    v <<= 4;
    v += getnibble(d, s);
    return v;
}

static int
dec_line(u_char **d, int l, uint32_t *out, dvdsub_image_t *dsi, uint32_t *plt)
{
    int s = 0;
    int v;

    do {
	int cnt, clr;
	v = getval(d, &s);
	cnt = v >> 2;
	clr = v & 3;
	l -= cnt;
	while(cnt--)
	    *out++ = plt[dsi->palette[clr]] | (dsi->alpha[clr] << 24);
    } while((v & ~3) && l > 0);

    while(l--)
	*out++ = plt[dsi->palette[v & 3]] | (dsi->alpha[v & 3] << 24);

    if(s)
	(*d)++;

    return 0;
}

static void
dvdsub_freepk(void *p)
{
    dvdsub_packet_t *pk = p;
    free(pk->data);
}

static int
dvdsub_decode_packet(tcvp_pipe_t *p, int str)
{
    dvdsub_t *ds = p->private;
    dvdsub_image_t dsi;
    dvdsub_packet_t *pk;
    uint32_t *buf;
    int i;

    tc2_print("DVDSUB", TC2_PRINT_DEBUG, "psize %x, bpos %x\n",
	      ds->psize, ds->bpos);

    memset(&dsi, 0, sizeof(dsi));
    dvdsub_info(&dsi, ds->buf, ds->psize);

    pk = tcallocdz(sizeof(*pk), NULL, dvdsub_freepk);
    pk->size = dsi.width * dsi.height * 4;
    pk->data = malloc(pk->size);
    pk->pk.type = TCVP_PKT_TYPE_DATA;
    pk->pk.stream = str;
    pk->pk.data = &pk->data;
    pk->pk.sizes = &pk->size;
    pk->pk.planes = 1;
    pk->pk.x = dsi.x;
    pk->pk.y = dsi.y;
    pk->pk.w = dsi.width;
    pk->pk.h = dsi.height;
    pk->pk.flags = TCVP_PKT_FLAG_PTS;
    pk->pk.pts = ds->pts + dsi.start * 270000;
    pk->pk.dts = ds->pts + dsi.end * 270000;

    buf = (uint32_t *) pk->data;

    for(i = 0; i < dsi.height; i++){
	dec_line((i & 1)? &dsi.odd: &dsi.even, dsi.width, buf,
		 &dsi, ds->palette);
	buf += dsi.width;
    }

    ds->bpos = 0;

    p->next->input(p->next, (tcvp_packet_t *) pk);

    return 0;
}

#define min(a,b) ((a)<(b)?(a):(b))

extern int
dvdsub_decode(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    dvdsub_t *ds = p->private;
    u_char *data;
    int size;

    if(!pk->data){
	p->next->input(p->next, (tcvp_packet_t *) pk);
	return 0;
    }

    data = pk->data[0];
    size = pk->sizes[0];

    while(size > 0){
	int cs;

	if(ds->bpos == 0){
	    if(!(pk->flags & TCVP_PKT_FLAG_PTS))
		break;
	    ds->psize = htob_16(unaligned16(data));
	    ds->pts = pk->pts;
	    tc2_print("DVDSUB", TC2_PRINT_DEBUG, "psize %x\n", ds->psize);
	}

	cs = min(ds->psize - ds->bpos, size);
	memcpy(ds->buf + ds->bpos, data, cs);
	ds->bpos += cs;
	data += cs;
	size -= cs;

	if(ds->bpos == ds->psize)
	    dvdsub_decode_packet(p, pk->stream);
    }

    tcfree(pk);
    return 0;
}

extern int
dvdsub_flush(tcvp_pipe_t *p, int drop)
{
    dvdsub_t *ds = p->private;
    if(drop)
	ds->bpos = 0;

    return 0;
}

static uint32_t dvdsub_palettes[][16] = {
    { 0x00788080,
      0x00e1807f,
      0x00228080,
      0x00ea8080,
      0x0084c453,
      0x0071bd8d,
      0x00d29210,
      0x005b4992,
      0x007b8080,
      0x00d18080,
      0x0030b66d,
      0x004f515b,
      0x0051f05a,
      0x00ea8080,
      0x00aeac38,
      0x00b45495 },

    { 0x00108080,
      0x00d58080,
      0x00eb8080,
      0x00298080,
      0x007d8080,
      0x00b48080,
      0x00a910a5,
      0x006addca,
      0x00d29210,
      0x001c76b8,
      0x0050505a,
      0x0030b86d,
      0x005d4792,
      0x003dafa5,
      0x00718947,
      0x00eb8080 }
};

#define NPALETTES (sizeof(dvdsub_palettes)/sizeof(dvdsub_palettes[0]))

extern int
dvdsub_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{
    dvdsub_t *ds = p->private;

    if(s->common.codec_data){
	ds->palette = s->common.codec_data;
    } else {
	int pl = tcvp_codec_dvdsub_conf_palette;
	if(pl >= NPALETTES || pl < 0)
	    pl = 0;
	ds->palette = dvdsub_palettes[pl];
    }

    p->format = *s;
    p->format.common.codec = "overlay/raw";
    return PROBE_OK;
}

static void
dvdsub_free(void *p)
{
    dvdsub_t *ds = p;
    free(ds->buf);
}

extern int
dvdsub_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs,
	   tcvp_timer_t *t, muxed_stream_t *ms)
{
    dvdsub_t *ds = tcallocdz(sizeof(*ds), NULL, dvdsub_free);
    ds->bsize = 0x10000;
    ds->buf = malloc(ds->bsize);

    p->format = *s;
    p->format.common.codec = "overlay/raw";
    p->private = ds;

    return 0;
}

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

#ifndef TCVP_BITS_H
#define TCVP_BITS_H 1

#include <tctypes.h>
#include <tcendian.h>

typedef struct tcvp_bits {
    u_char *data;
    size_t size;
    uint32_t bits;
    int bs;
} tcvp_bits_t;

#define bs_min(a,b) ((a)<(b)?(a):(b))

static inline void
tcvp_bits_init(tcvp_bits_t *s, u_char *bits, size_t size)
{
    s->data = bits;
    s->size = size;
    s->bits = 0;
    s->bs = 0;
}

static inline uint32_t
tcvp_bits_get(tcvp_bits_t *s, int bits)
{
    uint32_t v = 0;

    while(bits){
	int b;
	uint32_t m;

	if(!s->bs){
	    if(s->size * 8 < bits)
		return -1;

	    s->bits = htob_32(unaligned32(s->data));
	    s->data += 4;
	    s->size -= 4;
	    s->bs = 32;
	}

	b = bs_min(bits, s->bs);
	v <<= b;
	m = b < 32? (1 << b) - 1: -1;
	v |= (s->bits >> (32 - b)) & m;
	s->bits <<= b;
	s->bs -= b;
	bits -= b;
    }

    return v;
}

static inline void
tcvp_bits_put(tcvp_bits_t *s, uint32_t d, int bits)
{
    while(bits){
	int b = bs_min(bits, 32 - s->bs);
	uint32_t m = b < 32? (1 << b) - 1: -1;
	s->bits <<= b;
	s->bits |= (d >> (bits - b)) & m;
	s->bs += b;
	bits -= b;

	if(s->bs == 32){
	    st_unaligned32(htob_32(s->bits), s->data);
	    s->data += 4;
	    s->size -= 4;
	    s->bs = 0;
	}
    }
}

static inline void
tcvp_bits_flush(tcvp_bits_t *s)
{
    if(!s->bs)
	return;

    while(s->bs > 7){
	*s->data++ = s->bits >> (s->bs - 8);
	s->bs -= 8;
	s->size--;
    }

    if(s->bs){
	*s->data++ = s->bits << (8 - s->bs);
	s->bs = 0;
	s->size--;
    }
}

static inline uint32_t
tcvp_bits_getue(tcvp_bits_t *bs)
{
    int lzb = -1;
    uint32_t codenum;
    int b;

    for(b = 0; !b; lzb++)
	b = tcvp_bits_get(bs, 1);

    codenum = (1 << lzb) - 1 + tcvp_bits_get(bs, lzb);

    return codenum;
}

static inline int32_t
tcvp_bits_getse(tcvp_bits_t *bs)
{
    uint32_t codenum = tcvp_bits_getue(bs);
    int32_t val;

    val = (codenum + 1) / 2;
    if(!(codenum & 1))
	val = -val;

    return val;
}

#undef bs_min

#endif

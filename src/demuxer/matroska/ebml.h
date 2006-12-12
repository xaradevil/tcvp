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

#ifndef EBML_H
#define EBML_H

#include <tctypes.h>

typedef struct ebml_header {
    u_int ebmlversion;
    u_int ebmlreadversion;
    u_int ebmlmaxidlength;
    u_int ebmlmaxsizelength;
    char *doctype;
    u_int doctypeversion;
    u_int doctypereadversion;
} ebml_header_t;

#define EBML_ID_EBML			0x0a45dfa3
#define EBML_ID_EBMLVERSION		0x0286
#define EBML_ID_EBMLREADVERSION		0x02f7
#define EBML_ID_EBMLMAXIDLENGTH		0x02f2
#define EBML_ID_EBMLMAXSIZELENGTH	0x02f3
#define EBML_ID_DOCTYPE			0x0282
#define EBML_ID_DOCTYPEVERSION		0x0287
#define EBML_ID_DOCTYPEREADVERSION	0x0285
#define EBML_ID_CRC32			0x43
#define EBML_ID_CRC32VALUE		0x02fe
#define EBML_ID_VOID			0x6c

typedef int (*ebml_element_callback_t)(uint64_t id, uint64_t size, void *);

#define EBML_CB_SUCCESS	0
#define EBML_CB_UNKNOWN	1
#define EBML_CB_ERROR	-1

extern uint64_t ebml_get_vint(url_t *u, int *s);
extern uint64_t ebml_get_int(url_t *u, int size);
extern double ebml_get_float(url_t *u, int size);
extern char *ebml_get_string(url_t *u, int size);
extern uint8_t *ebml_get_binary(url_t *u, int size);

extern int ebml_readheader(url_t *u, ebml_header_t *eh);
extern int ebml_element(url_t *u, uint64_t *id, uint64_t *size, uint64_t *ps);
extern int ebml_read_elements(url_t *u, uint64_t size, ebml_element_callback_t,
			      void *cbdata);

#endif

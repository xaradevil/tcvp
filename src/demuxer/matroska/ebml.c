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
#include <tctypes.h>
#include <tcendian.h>
#include <math.h>
#include <matroska_tc2.h>
#include "ebml.h"

extern uint64_t
ebml_get_int(url_t *u, int size)
{
    u_char buf[8], *bp = buf;
    uint64_t val = 0;

    if(u->read(buf, 1, size, u) < size)
        return -1;

    while(size--){
        val <<= 8;
        val += *bp++;
    }

    return val;
}

extern double
ebml_get_float(url_t *u, int size)
{
    union {
        uint32_t u32;
        uint64_t u64;
        float f;
        double d;
        long double ld;
        u_char buf[10];
    } rv;
    double val = -1;

    if(size == 0)
        return 0.0;

    if(size != 4 && size != 8 && size != 10){
        tc2_print("EBML", TC2_PRINT_WARNING, "invalid float size %i\n", size);
        return -1;
    }

    if(u->read(rv.buf, 1, size, u) < size)
        return -1;

    if(size == 4){
        rv.u32 = htob_32(rv.u32);
        val = rv.f;
    } else if(size == 8) {
        rv.u64 = htob_64(rv.u64);
        val = rv.d;
    } else if(size == 10) {
#if TCENDIAN == TCENDIAN_LITTLE
        int i;
        for(i = 0; i < 5; i++){
            u_char t = rv.buf[i];
            rv.buf[i] = rv.buf[9 - i];
            rv.buf[9 - i] = t;
        }
#endif
        val = rv.ld;
    }

    return val;
}

extern char *
ebml_get_string(url_t *u, int size)
{
    char *s = calloc(1, size + 1);

    if(!s)
        return NULL;

    if(u->read(s, 1, size, u) < size){
        free(s);
        return NULL;
    }

    s[size] = 0;

    return s;
}

extern uint8_t *
ebml_get_binary(url_t *u, int size, int padding)
{
    uint8_t *s = calloc(1, size + padding);

    if(!s)
        return NULL;

    if(u->read(s, 1, size, u) < size){
        free(s);
        return NULL;
    }

    return s;
}

extern uint64_t
ebml_get_vint(url_t *u, int *vs)
{
    int size = 0, a;
    uint64_t val;

    val = a = url_getc(u);

    if(!a || a == -1)
        return -1;

    while(a < 0x80){
        size++;
        a <<= 1;
    }

    val &= 0x7f >> size;

    if(size > 0){
        uint64_t tv = ebml_get_int(u, size);
        if(tv == -1)
            return -1;
        val <<= size * 8;
        val += tv;
    }

    if(vs)
        *vs = size + 1;

    return val;
}

extern int
ebml_element(url_t *u, uint64_t *id, uint64_t *size, uint64_t *psize)
{
    int ss;

    *id = ebml_get_vint(u, &ss);
    if(*id == -1)
        return -1;

    if(psize)
        *psize -= ss;

    *size = ebml_get_vint(u, &ss);
    if(*size == -1)
        return -1;

    if(psize)
        *psize -= ss;

    tc2_print("EBML", TC2_PRINT_DEBUG+1, "id %8llx, size %4lli\n", *id, *size);

    if(psize && *size > *psize){
        tc2_print("EBML", TC2_PRINT_WARNING,
                  "element %llx size %lli exceeds parent size @%lli\n",
                  *id, *size, u->tell(u));
        return -1;
    }

    return 0;
}

extern int
ebml_read_elements(url_t *u, uint64_t psize, ebml_element_callback_t cb,
                   void *cbdata)
{
    uint64_t id, size;

    while(psize && !ebml_element(u, &id, &size, &psize)){
        int r = cb(id, size, cbdata);

        switch(r){
        case EBML_CB_SUCCESS:
            /* do nothing */
            break;
        case EBML_CB_UNKNOWN:
            u->seek(u, size, SEEK_CUR);
            break;
        case EBML_CB_BREAK:
            return 0;
        case EBML_CB_ERROR:
            return -1;
        }

        psize -= size;
    }

    return 0;
}

extern int
ebml_readheader(url_t *u, ebml_header_t *eh)
{
    uint32_t eid;
    uint64_t hsize;

    eh->ebmlversion = 1;
    eh->ebmlreadversion = 1;
    eh->ebmlmaxidlength = 4;
    eh->ebmlmaxsizelength = 8;
    eh->doctype = NULL;
    eh->doctypeversion = 1;
    eh->doctypereadversion = 1;

    if(url_getu32b(u, &eid) < 0 || eid != (EBML_ID_EBML | 0x10000000)){
        tc2_print("EBML", TC2_PRINT_DEBUG, "EBML header not found\n");
        return -1;
    }

    hsize = ebml_get_vint(u, NULL);
    tc2_print("EBML", TC2_PRINT_DEBUG, "EBML header size %i\n", hsize);

    while(hsize > 0){
        uint64_t id, size;

        if(ebml_element(u, &id, &size, &hsize) < 0)
            return -1;

        switch(id){
        case EBML_ID_EBMLVERSION:
            eh->ebmlversion = ebml_get_int(u, size);
            break;
        case EBML_ID_EBMLREADVERSION:
            eh->ebmlreadversion = ebml_get_int(u, size);
            break;
        case EBML_ID_EBMLMAXIDLENGTH:
            eh->ebmlmaxidlength = ebml_get_int(u, size);
            break;
        case EBML_ID_EBMLMAXSIZELENGTH:
            eh->ebmlmaxsizelength = ebml_get_int(u, size);
            break;
        case EBML_ID_DOCTYPE:
            eh->doctype = ebml_get_string(u, size);
            if(!eh->doctype)
                return -1;
            break;
        case EBML_ID_DOCTYPEVERSION:
            eh->doctypeversion = ebml_get_int(u, size);
            break;
        case EBML_ID_DOCTYPEREADVERSION:
            eh->doctypereadversion = ebml_get_int(u, size);
            break;
        default:
            tc2_print("EBML", TC2_PRINT_WARNING,
                      "unknown element id %llx in header\n", id);
            break;
        }

        hsize -= size;
    }

    tc2_print("EBML", TC2_PRINT_DEBUG, "version %i, readversion %i\n",
              eh->ebmlversion, eh->ebmlreadversion);
    tc2_print("EBML", TC2_PRINT_DEBUG,
              "doctype '%s', version %i, readversion %i\n",
              eh->doctype, eh->doctypeversion, eh->doctypereadversion);

    return 0;
}

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

#ifndef FLAC_H
#define FLAC_H

typedef struct flac_metadata {
    int type;
    int last;
    int size;
    u_char *data;
} flac_metadata_t;

#define FLAC_META_STREAMINFO     0
#define FLAC_META_PADDING        1
#define FLAC_META_APPLICATION    2
#define FLAC_META_SEEKTABLE      3
#define FLAC_META_VORBIS_COMMENT 4
#define FLAC_META_CUESHEET       5
#define FLAC_META_INVALID      127

extern uint8_t flac_crc8(const uint8_t *data, unsigned len);
extern uint16_t flac_crc16(const uint8_t *data, unsigned len);

#endif

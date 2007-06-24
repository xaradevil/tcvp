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

#ifndef _MPEG_H
#define _MPEG_H

#include <tctypes.h>
#include <tcvp_types.h>
#include <mpeg_tc2.h>

typedef struct mpegpes_packet {
    int stream_id;
    int flags;
    uint64_t pts, dts;
    int size;
    u_char *data;
    u_char *hdr;
} mpegpes_packet_t;

#define PES_FLAG_PTS 0x1
#define PES_FLAG_DTS 0x2

typedef struct mpeg_stream_type {
    int mpeg_stream_type;
    int stream_id_base;
    char *codec;
} mpeg_stream_type_t;

#define MPEGTS_SYNC 0x47

#define PACK_HEADER              0xba
#define SYSTEM_HEADER            0xbb

#define PROGRAM_STREAM_MAP       0xbc
#define PRIVATE_STREAM_1         0xbd
#define PADDING_STREAM           0xbe
#define PRIVATE_STREAM_2         0xbf
#define ECM_STREAM               0xf0
#define EMM_STREAM               0xf1
#define DSMCC_STREAM             0xf2
#define ISO_13522_STREAM         0xf3
#define H222_A_STREAM            0xf4
#define H222_B_STREAM            0xf5
#define H222_C_STREAM            0xf6
#define H222_D_STREAM            0xf7
#define H222_E_STREAM            0xf8
#define ANCILLARY_STREAM         0xf9
#define ISO_14496_SL_STREAM      0xfa
#define ISO_14496_FLEXMUX_STREAM 0xfb
#define PROGRAM_STREAM_DIRECTORY 0xff

#define VIDEO_STREAM_DESCRIPTOR                  2
#define AUDIO_STREAM_DESCRIPTOR                  3
#define HIERARCHY_DESCRIPTOR                     4
#define REGISTRATION_DESCRIPTOR                  5
#define DATA_STREAM_ALIGNMENT_DESCRIPTOR         6
#define TARGET_BACKGROUND_GRID_DESCRIPTOR        7
#define VIDEO_WINDOW_DESCRIPTOR                  8
#define CA_DESCRIPTOR                            9
#define ISO_639_LANGUAGE_DESCRIPTOR             10
#define SYSTEM_CLOCK_DESCRIPTOR                 11
#define MULTIPLEX_BUFFER_UTILIZATION_DESCRIPTOR 12
#define COPYRIGHT_DESCRIPTOR                    13
#define MAXIMUM_BITRATE_DESCRIPTOR              14
#define PRIVATE_DATA_INDICATOR_DESCRIPTOR       15
#define SMOOTHING_BUFFER_DESCRIPTOR             16
#define STD_DESCRIPTOR                          17
#define IBP_DESCRIPTOR                          18

#define DVB_NETWORK_NAME_DESCRIPTOR                 0x40
#define DVB_SERVICE_LIST_DESCRIPTOR                 0x41
#define DVB_STUFFING_DESCRIPTOR                     0x42
#define DVB_SATELLITE_DELIVERY_SYSTEM_DESCRIPTOR    0x43
#define DVB_CABLE_DELIVERY_SYSTEM_DESCRIPTOR        0x44
#define DVB_VBI_DATA_DESCRIPTOR                     0x45
#define DVB_VBI_TELETEXT_DESCRIPTOR                 0x46
#define DVB_BOUQUET_NAME_DESCRIPTOR                 0x47
#define DVB_SERVICE_DESCRIPTOR                      0x48
#define DVB_COUNTRY_AVAILABILITY_DESCRIPTOR         0x49
#define DVB_LINKAGE_DESCRIPTOR                      0x4a
#define DVB_NVOD_REFERENCE_DESCRIPTOR               0x4b
#define DVB_TIME_SHIFTED_SERVICE_DESCRIPTOR         0x4c
#define DVB_SHORT_EVENT_DESCRIPTOR                  0x4d
#define DVB_EXTENDED_EVENT_DESCRIPTOR               0x4e
#define DVB_TIME_SHIFTED_EVENT_DESCRIPTOR           0x4f
#define DVB_COMPONENT_DESCRIPTOR                    0x50
#define DVB_MOSAIC_DESCRIPTOR                       0x51
#define DVB_STREAM_IDENTIFIER_DESCRIPTOR            0x52
#define DVB_CA_IDENTIFIER_DESCRIPTOR                0x53
#define DVB_CONTENT_DESCRIPTOR                      0x54
#define DVB_PARENTAL_RATING_DESCRIPTOR              0x55
#define DVB_TELETEXT_DESCRIPTOR                     0x56
#define DVB_TELEPHONE_DESCRIPTOR                    0x57
#define DVB_LOCAL_TIME_OFFSET_DESCRIPTOR            0x58
#define DVB_SUBTITLING_DESCRIPTOR                   0x59
#define DVB_TERRESTRIAL_DELIVERY_SYSTEM_DESCRIPTOR  0x5a
#define DVB_MULTILINGUAL_NETWORK_NAME_DESCRIPTOR    0x5b
#define DVB_MULTILINGUAL_BOUQUET_NAME_DESCRIPTOR    0x5c
#define DVB_MULTILINGUAL_SERVICE_NAME_DESCRIPTOR    0x5d
#define DVB_MULTILINGUAL_COMPONENT_DESCRIPTOR       0x5e
#define DVB_PRIVATE_DATA_SPECIFIER_DESCRIPTOR       0x5f
#define DVB_SERVICE_MOVE_DESCRIPTOR                 0x60
#define DVB_SHORT_SMOOTHING_BUFFER_DESCRIPTOR       0x61
#define DVB_FREQUENCY_LIST_DESCRIPTOR               0x62
#define DVB_PARTIAL_TRANSPORT_STREAM_DESCRIPTOR     0x63
#define DVB_DATA_BROADCAST_DESCRIPTOR               0x64
#define DVB_SCRAMBLING_DESCRIPTOR                   0x65
#define DVB_DATA_BROADCAST_ID_DESCRIPTOR            0x66
#define DVB_TRANSPORT_STREAM_DESCRIPTOR             0x67
#define DVB_DSNG_DESCRIPTOR                         0x68
#define DVB_PDC_DESCRIPTOR                          0x69
#define DVB_AC_3_DESCRIPTOR                         0x6a
#define DVB_ANCILLARY_DATA_DESCRIPTOR               0x6b
#define DVB_CELL_LIST_DESCRIPTOR                    0x6c
#define DVB_CELL_FREQUENCY_LINK_DESCRIPTOR          0x6d
#define DVB_ANNOUNCEMENT_SUPPORT_DESCRIPTOR         0x6e
#define DVB_APPLICATION_SIGNALLING_DESCRIPTOR       0x6f
#define DVB_ADAPTATION_FIELD_DATA_DESCRIPTOR        0x70
#define DVB_SERVICE_IDENTIFIER_DESCRIPTOR           0x71
#define DVB_SERVICE_AVAILABILITY_DESCRIPTOR         0x72
#define DVB_DEFAULT_AUTHORITY_DESCRIPTOR            0x73
#define DVB_RELATED_CONTENT_DESCRIPTOR              0x74
#define DVB_TVA_ID_DESCRIPTOR                       0x75
#define DVB_CONTENT_IDENTIFIER_DESCRIPTOR           0x76
#define DVB_TIME_SLICE_FEC_IDENTIFIER_DESCRIPTOR    0x77
#define DVB_ECM_REPETITION_RATE_DESCRIPTOR          0x78
#define DVB_S2_SATELLITE_DELIVERY_SYSTEM_DESCRIPTOR 0x79
#define DVB_ENHANCED_AC_3_DESCRIPTOR                0x7a
#define DVB_DTS_DESCRIPTOR                          0x7b
#define DVB_AAC_DESCRIPTOR                          0x7c
#define DVB_EXTENSION_DESCRIPTOR                    0x7f

#define ISVIDEO(id) ((id & 0xf0) == 0xe0)
#define ISMPEGAUDIO(id) ((id & 0xe0) == 0xc0)
#define ISAC3(id) ((id & 0xf8) == 0x80)
#define ISDTS(id) ((id & 0xf8) == 0x88)
#define ISPCM(id) ((id & 0xf8) == 0xa0)
#define ISSPU(id) ((id & 0xe0) == 0x20)
#define ISPS1AC3(pk) ((pk->stream_id == 0x0b) && pk->data[1] == 0x77)

#define min(a,b) ((a)<(b)?(a):(b))

extern int mpegpes_header(mpegpes_packet_t *pes, u_char *data, int h);
extern mpeg_stream_type_t *mpeg_stream_type_id(int st);
extern mpeg_stream_type_t *mpeg_stream_type(char *codec);
extern int mpeg_descriptor(stream_t *s, u_char *d);
extern int write_mpeg_descriptor(stream_t *s, int tag, u_char *d, int size);
extern int write_pes_header(u_char *p, int stream_id, int size,
			    int flags, ...);
extern uint32_t mpeg_crc32(const u_char *data, int len);
extern void mpeg_free(muxed_stream_t *);

extern muxed_stream_t *mpegts_open(char *, url_t *, tcconf_section_t *,
				   tcvp_timer_t *);

extern muxed_stream_t *mpegps_open(char *, url_t *, tcconf_section_t *,
				   tcvp_timer_t *);

#endif

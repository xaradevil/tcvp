/**
    Copyright (C) 2003-2007  Michael Ahlberg, Måns Rullgård

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
#include <stddef.h>
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcendian.h>
#include <tcvp_types.h>
#include <mpeg_tc2.h>
#include "mpeg.h"

#define TS_PACKET_BUF 7
#define TS_PACKET_SIZE 188

#define MAX_PACKET_SIZE 0x10000

typedef struct mpegts_packet {
    int transport_error;
    int unit_start;
    int priority;
    int pid;
    int scrambling;
    int adaptation;
    int cont_counter;
    struct adaptation_field {
        int discontinuity;
        int random_access;
        int es_priority;
        int pcr_flag;
        int opcr_flag;
        int splicing_point;
        int transport_private;
        int extension;
        uint64_t pcr;
        uint64_t opcr;
        int splice_countdown;
    } adaptation_field;
    int data_length;
    uint8_t *data;
} mpegts_packet_t;

#define MPEGTS_SECTION_COMMON_LEN 8

#define MPEGTS_TABLE_ID_PAT 0
#define MPEGTS_TABLE_ID_PMT 2

#define MPEGTS_PSI_NO_VERSION 0x20

typedef struct mpegts_section {
    unsigned int table_id;
    unsigned int length;
    unsigned int id;
    unsigned int version;
    unsigned int current;
    unsigned int number;
    unsigned int last;
    unsigned int read;
    uint8_t buf[1024];
} mpegts_section_t;

typedef struct mpegts_elem_stream {
    unsigned int stream_type;
    unsigned int pid;
    unsigned int es_info_length;
    uint8_t *descriptors;
} mpegts_elem_stream_t;

typedef struct mpegts_program {
    unsigned int program_number;
    unsigned int program_map_pid;
    unsigned int pcr_pid;
    unsigned int program_info_length;
    uint8_t *descriptors;
    unsigned int pmt_version;
    unsigned int num_streams;
    mpegts_elem_stream_t *streams;
    mpegts_section_t *psi;
} mpegts_program_t;

#define MPEGTS_PID_TYPE_PSI 1
#define MPEGTS_PID_TYPE_ES  2
#define MPEGTS_PID_TYPE_NIT 3

#define MPEGTS_PID_TYPE(x)  ((x) & 255)
#define MPEGTS_PID_INDEX(x) ((unsigned)(x) >> 8)
#define MPEGTS_PID_MAP(type, idx) ((type) | (idx) << 8)

#define MPEGTS_PID_NO_INDEX 0xffffff
#define MPEGTS_PID_PSI_NO_INDEX                                 \
    MPEGTS_PID_MAP(MPEGTS_PID_TYPE_PSI, MPEGTS_PID_NO_INDEX)

typedef struct mpegts_stream {
    url_t *stream;
    uint8_t *tsbuf, *tsp;
    int tsnbuf;
    int extra;
    uint32_t *pidmap;
    int pcrpid;
    unsigned int pat_version;
    mpegts_program_t *programs;
    unsigned int num_programs;
    mpegts_section_t *psi;
    unsigned int nit_pid;
    struct tsbuf {
        int flags;
        uint64_t pts, dts;
        uint8_t *buf;
        int bpos;
        int hlen;
        int cc;
        int start;
    } *streams;
    int rate;
    uint64_t start_time;
    int end;
    mpegts_packet_t mp;
} mpegts_stream_t;

typedef struct mpegts_pk {
    tcvp_data_packet_t pk;
    uint8_t *buf, *data;
    int size;
} mpegts_pk_t;

#define getbit(v, b) ((v >> b) & 1)

static int
fill_buf(mpegts_stream_t *s)
{
    int bpos = (s->tsp - s->tsbuf) % 188;
    int n = s->tsnbuf * TS_PACKET_SIZE + s->extra;
    int nb = s->tsnbuf;
    int eof = 0;

    memmove(s->tsbuf, s->tsp - bpos, n);
    s->tsp = s->tsbuf + bpos;

    while(s->tsnbuf < TS_PACKET_BUF && !eof){
        int r = TS_PACKET_SIZE * TS_PACKET_BUF - n;
        r = s->stream->read(s->tsbuf + n, 1, r, s->stream);
        if(r <= 0){
            if(s->tsnbuf <= nb)
                return -1;
            eof = 1;
        }

        n += r;
        s->tsnbuf = n / TS_PACKET_SIZE;
    }

    s->extra = n - s->tsnbuf * TS_PACKET_SIZE;

    return 0;
}

static int
resync(mpegts_stream_t *s)
{
    if(s->tsnbuf < 2){
        if(fill_buf(s) < 0)
            return -1;
    }

    while(s->tsp[0] != MPEGTS_SYNC || s->tsp[188] != MPEGTS_SYNC){
        ptrdiff_t bpos = s->tsp - s->tsbuf;
        uint8_t *bufmax =
            s->tsp - bpos % 188 + (s->tsnbuf - 1) * TS_PACKET_SIZE + s->extra;
        int sync = 0;
        int nb;

        while(s->tsp < bufmax){
            if(s->tsp[0] == MPEGTS_SYNC && s->tsp[188] == MPEGTS_SYNC){
                sync = 1;
                break;
            }
            s->tsp++;
        }

        nb = bufmax - s->tsp + TS_PACKET_SIZE;
        memmove(s->tsbuf, s->tsp, nb);
        s->tsp = s->tsbuf + nb;
        s->tsnbuf = nb / TS_PACKET_SIZE;

        if(sync){
            int rb;

            nb -= s->tsnbuf * TS_PACKET_SIZE;
            rb = 188 - nb;

            if(nb > 0){
                rb = s->stream->read(s->tsp, 1, rb, s->stream);
                if(rb < 0)
                    return -1;
                nb += rb;
                if(nb == 188){
                    s->tsnbuf++;
                    nb = 0;
                }
            }
            s->tsp = s->tsbuf;
            s->extra = nb;
        } else if(fill_buf(s)){
            return -1;
        }
    }

    s->tsp = s->tsbuf;
    return 0;
}

static void
skip_packet(mpegts_stream_t *s)
{
    ptrdiff_t bp = s->tsp - s->tsbuf;
    s->tsp += TS_PACKET_SIZE - bp % TS_PACKET_SIZE;
    s->tsnbuf--;
}

static uint64_t
get_pcr(uint8_t *p)
{
    uint64_t pcr;

    pcr = ((uint64_t) p[0] << 25) +
        (p[1] << 17) +
        (p[2] << 9) +
        (p[3] << 1) +
        ((p[4] & 0x80) >> 7);
    pcr *= 300;
    pcr += ((p[4] & 1) << 8) + p[5];

    return pcr;
}

static int
mpegts_read_packet(mpegts_stream_t *s, mpegts_packet_t *mp)
{
    int error = 0, skip = 0;

#define do_error() do {                         \
    skip_packet(s);                             \
    error = -1;                                 \
    skip++;                                     \
    goto next;                                  \
} while(0)

#define check_length(l, start, len, m) do {                             \
    if(l > start + len - s->tsp || l < 0){                              \
        tc2_print("MPEGTS", TC2_PRINT_WARNING, "PID %x: " m, mp->pid, l); \
        do_error();                                                     \
    }                                                                   \
} while(0)

    do {
        uint8_t *pkstart;
        unsigned v;

        error = 0;

        if(!s->tsnbuf){
            if(fill_buf(s))
                return -1;
        }

        pkstart = s->tsp;

        if(*s->tsp != MPEGTS_SYNC){
            tc2_print("MPEGTS", TC2_PRINT_WARNING,
                      "bad sync byte %02x @%ti, buf %i, %llx\n",
                      *s->tsp, s->tsp - s->tsbuf, s->tsnbuf,
                      s->stream->tell(s->stream));
            if(resync(s))
                return -1;
            skip++;
            error = 1;
            continue;
        }

        s->tsp++;
        v = *s->tsp++;
        mp->transport_error = getbit(v, 7);
        if(mp->transport_error){
            tc2_print("MPEGTS", TC2_PRINT_WARNING, "transport error\n");
            do_error();
        }

        mp->unit_start = getbit(v, 6);
        mp->priority = getbit(v, 5);
        mp->pid = (v & 0x1f) << 8 | *s->tsp++;

        v = *s->tsp++;
        mp->scrambling = (v >> 6) & 3;
        mp->adaptation = (v >> 4) & 3;
        mp->cont_counter = v & 0xf;

        if(mp->adaptation & 2){
            struct adaptation_field *af = &mp->adaptation_field;
            int al = *s->tsp++;
            check_length(al, pkstart, TS_PACKET_SIZE,
                         "invalid adaptation field length %i\n");

            if(al > 0){
                uint8_t *afstart = s->tsp;
                int stuffing;

                v = *s->tsp++;

                af->discontinuity = getbit(v, 7);
                af->random_access = getbit(v, 6);
                af->es_priority = getbit(v, 5);
                af->pcr_flag = getbit(v, 4);
                af->opcr_flag = getbit(v, 3);
                af->splicing_point = getbit(v, 2);
                af->transport_private = getbit(v, 1);
                af->extension = getbit(v, 0);

                if(af->pcr_flag){
                    af->pcr = get_pcr(s->tsp);
                    s->tsp += 6;
                }
                if(af->opcr_flag){
                    af->opcr = get_pcr(s->tsp);
                    s->tsp += 6;
                }
                if(af->splicing_point)
                    af->splice_countdown = *s->tsp++;
                if(af->transport_private){
                    int tl = *s->tsp++;
                    check_length(tl, afstart, al,
                                 "invalid transport_private length %i\n");
                    s->tsp += tl;
                }
                if(af->extension){
                    int afel = *s->tsp++;
                    check_length(afel, afstart, al,
                                 "invalid adaptation_field_extension_"
                                 "length %i\n");
                    s->tsp += afel;
                }

                stuffing = al - (s->tsp - afstart);

                check_length(stuffing, pkstart, TS_PACKET_SIZE,
                             "BUG: stuffing = %i\n");
                while(stuffing--){
                    if(*s->tsp++ != 0xff){
                        tc2_print("MPEGTS", TC2_PRINT_WARNING,
                                  "PID %x, stuffing != 0xff: %x\n",
                                  mp->pid, *(s->tsp-1));
                        do_error();
                    }
                }
            }
        }

        if(mp->adaptation & 1){
            ptrdiff_t hl = s->tsp - pkstart;
            mp->data_length = TS_PACKET_SIZE - hl;
            check_length(mp->data_length, pkstart, TS_PACKET_SIZE,
                         "BUG: data_length = %i\n");
            mp->data = s->tsp;
        } else {
            mp->data_length = 0;
        }

        s->tsp += mp->data_length;
        s->tsnbuf--;
      next:;
    } while(error && skip < tcvp_demux_mpeg_conf_ts_max_skip);

    return error;
#undef do_error
#undef check_length
}

static void
mpegts_free_pk(void *p)
{
    mpegts_pk_t *mp = p;
    free(mp->buf);
}

static tcvp_packet_t *
mpegts_mkpacket(mpegts_stream_t *s, int sx)
{
    struct tsbuf *tb = s->streams + sx;
    mpegts_pk_t *pk;

    pk = tcallocdz(sizeof(*pk), NULL, mpegts_free_pk);
    pk->pk.stream = sx;
    pk->pk.data = &pk->data;
    pk->data = tb->buf + tb->hlen;
    pk->buf = tb->buf;
    pk->pk.sizes = &pk->size;
    pk->size = tb->bpos - tb->hlen;
    pk->pk.planes = 1;
    pk->pk.flags = tb->flags;
    if(tb->flags & TCVP_PKT_FLAG_PTS)
        pk->pk.pts = tb->pts * 300;
    if(tb->flags & TCVP_PKT_FLAG_DTS)
        pk->pk.dts = tb->dts * 300;

    memset(pk->data + pk->size, 0, 8);

    tb->buf = malloc(2 * MAX_PACKET_SIZE);

    return (tcvp_packet_t *) pk;
}

static tcvp_packet_t *
mpegts_endpacket(muxed_stream_t *ms)
{
    mpegts_stream_t *s = ms->private;
    tcvp_packet_t *pk = NULL;

    while(++s->end < ms->n_streams && !ms->used_streams[s->end]);

    if(s->end >= ms->n_streams)
        return NULL;

    if(s->streams[s->end].bpos)
        pk = mpegts_mkpacket(s, s->end);

    return pk;
}

static tcvp_packet_t *
mpegts_packet(muxed_stream_t *ms, int str)
{
    mpegts_stream_t *s = ms->private;
    tcvp_packet_t *pk = NULL;
    unsigned int sx, stype;
    struct tsbuf *tb;
#define mp s->mp

    if(s->end > -1)
        return mpegts_endpacket(ms);

    do {
        int ccd, pid;

        do {
            if(mp.pid < 0){
                if(mpegts_read_packet(s, &mp) < 0){
                    return mpegts_endpacket(ms);
                }
            }

            sx = MPEGTS_PID_INDEX(s->pidmap[mp.pid]);
            stype = MPEGTS_PID_TYPE(s->pidmap[mp.pid]);

            if(mp.pid == s->pcrpid &&
               (mp.adaptation & 2) && mp.adaptation_field.pcr_flag){
                if(s->start_time != -1LL){
                    uint64_t time =
                        (mp.adaptation_field.pcr - s->start_time) / 27000;
                    if(time)
                        s->rate = s->stream->tell(s->stream) / time;
                } else {
                    s->start_time = mp.adaptation_field.pcr;
                }

                if(s->stream->flags & URL_FLAG_STREAMED){
                    mp.adaptation_field.pcr_flag = 0;
                    pk = tcallocz(sizeof(*pk));
                    pk->type = TCVP_PKT_TYPE_TIMER;
                    pk->timer.time = mp.adaptation_field.pcr;
                    return pk;
                }
            }
            pid = mp.pid;
            mp.pid = -1;
        } while(stype != MPEGTS_PID_TYPE_ES || !ms->used_streams[sx]);

        tb = &s->streams[sx];

        if(tb->cc > -1){
            ccd = (mp.cont_counter - tb->cc + 0x10) & 0xf;
            if(ccd == 0){
                tc2_print("MPEGTS", TC2_PRINT_VERBOSE,
                          "PID %x, duplicate packet, cc = %x\n", pid,
                          mp.cont_counter);
                continue;
            } else if(ccd != 1){
                tc2_print("MPEGTS", TC2_PRINT_WARNING,
                          "PID %x, lost %i packets: %i %i\n",
                          pid, ccd - 1, tb->cc, mp.cont_counter);
/*              tb->start = 0; */
            }
        }
        tb->cc = mp.cont_counter;

        if((mp.unit_start && tb->bpos) || tb->bpos > MAX_PACKET_SIZE){
            if(tb->start)
                pk = mpegts_mkpacket(s, sx);
            tb->bpos = 0;
            tb->flags = 0;
            tb->hlen = 0;
        }

        memcpy(tb->buf + tb->bpos, mp.data, mp.data_length);
        tb->bpos += mp.data_length;

        if(mp.unit_start){
            mpegpes_packet_t pes;
            if(mpegpes_header(&pes, tb->buf, 0) < 0)
                return NULL;
            tb->hlen = pes.data - tb->buf;
            if(pes.flags & PES_FLAG_PTS){
                tb->flags |= TCVP_PKT_FLAG_PTS;
                tb->pts = pes.pts;
/*              fprintf(stderr, "MPEGTS: %i pts %lli\n", sx, pes.pts * 300); */
            }
            if(pes.flags & PES_FLAG_DTS){
                tb->flags |= TCVP_PKT_FLAG_DTS;
                tb->dts = pes.dts;
/*              fprintf(stderr, "MPEGTS: %i dts %lli\n", sx, pes.dts * 300); */
            }
            tb->start = 1;
        }
    } while(!pk);

    return pk;
#undef mp
}

#define absdiff(a,b) ((a)>(b)?(a)-(b):(b)-(a))

static uint64_t
mpegts_seek(muxed_stream_t *ms, uint64_t time)
{
    mpegts_stream_t *s = ms->private;
    int64_t p, st;
    int i, sm = SEEK_SET, c = 0;

    p = time / 27000 * s->rate;

    do {
        p /= 188;
        p *= 188;

        if(s->stream->seek(s->stream, p, sm))
            return -1;

        for(i = 0; i < ms->n_streams; i++){
            s->streams[i].flags = 0;
            s->streams[i].bpos = 0;
            s->streams[i].start = 0;
            s->streams[i].cc = -1;
        }

        st = 0;

        do {
            tcvp_packet_t *pk = mpegts_packet(ms, 0);
            if(pk && pk->type == TCVP_PKT_TYPE_DATA){
                if(pk->data.flags & TCVP_PKT_FLAG_PTS)
                    st = pk->data.pts;
                tcfree(pk);
            } else {
                return -1;
            }
        } while(!st);

        p = ((int64_t)time - st) / 27000 * s->rate;
        sm = SEEK_CUR;
    } while(absdiff(st, time) > 27000000 && c++ < 64);

    return st;
}

static void
mpegts_free_program(mpegts_program_t *p)
{
    int i;

    for(i = 0; i < p->num_streams; i++){
        free(p->streams[i].descriptors);
    }

    free(p->descriptors);
    free(p->streams);
    free(p->psi);

    p->pmt_version = MPEGTS_PSI_NO_VERSION;
}

static void
mpegts_free_programs(mpegts_stream_t *s)
{
    int i;

    for(i = 0; i < s->num_programs; i++){
        mpegts_free_program(s->programs + i);
    }

    free(s->programs);

    s->pat_version = MPEGTS_PSI_NO_VERSION;
}

static void
mpegts_free(void *p)
{
    muxed_stream_t *ms = p;
    mpegts_stream_t *s = ms->private;
    int i;

    if(s->stream)
        s->stream->close(s->stream);
    free(s->pidmap);
    if(s->streams){
        for(i = 0; i < ms->n_streams; i++)
            free(s->streams[i].buf);
        free(s->streams);
    }

    mpegts_free_programs(s);

    free(s->tsbuf);
    free(s->psi);
    free(s);
    mpeg_free(ms);
}

static mpegts_program_t *
mpegts_find_program(mpegts_stream_t *s, unsigned int program)
{
    unsigned int i;

    for(i = 0; i < s->num_programs; i++){
        if(s->programs[i].program_number == program){
            return s->programs + i;
        }
    }

    return NULL;
}

static int
mpegts_parse_pat(mpegts_stream_t *s, mpegts_section_t *psi)
{
    uint8_t *dp = psi->buf;
    unsigned int size = psi->length;
    unsigned int n;
    unsigned int i;

    if(psi->version == s->pat_version)
        return 0;

    if(size < 12)
        return -1;

    if(mpeg_crc32(dp, size)){
        tc2_print("MPEGTS", TC2_PRINT_WARNING, "Bad CRC in PAT.\n");
    }

    if(psi->number || psi->last){
        tc2_print("MPEGTS", TC2_PRINT_ERROR,
                  "BUG: Multi-section PAT not supported.\n");
        return -1;
    }

    mpegts_free_programs(s);

    n = (size - 12) / 4;
    s->programs = calloc(n, sizeof(*s->programs));
    s->num_programs = 0;
    s->pat_version = psi->version;

    dp += 8;
    for(i = 0; i < n; i++){
        int prg = htob_16(unaligned16(dp));
        int pid = htob_16(unaligned16(dp + 2)) & 0x1fff;

        tc2_print("MPEGTS", TC2_PRINT_DEBUG, "program %i => PMT pid %x\n",
                  prg, pid);

        if(prg){
            mpegts_program_t *pg = s->programs + s->num_programs++;
            s->pidmap[pid] = MPEGTS_PID_PSI_NO_INDEX;
            pg->program_number = prg;
            pg->program_map_pid = pid;
            pg->pmt_version = MPEGTS_PSI_NO_VERSION;
        } else {
            s->pidmap[pid] = MPEGTS_PID_MAP(MPEGTS_PID_TYPE_NIT, i);
            s->nit_pid = pid;
        }

        dp += 4;
    }

    return 0;
}

static int
mpegts_parse_pmt(mpegts_stream_t *s, mpegts_section_t *psi)
{
    mpegts_program_t *mp = NULL;
    uint8_t *dp = psi->buf;
    unsigned int size = psi->length;
    unsigned int pi_len;
    unsigned int ns;
    unsigned int i;

    if(size < 16)
        return -1;

    if(mpeg_crc32(dp, size) != 0){
        tc2_print("MPEGTS", TC2_PRINT_WARNING, "Bad CRC in PMT\n");
    }

    mp = mpegts_find_program(s, psi->id);

    if(!mp){
        tc2_print("MPEGTS", TC2_PRINT_WARNING, "PMT: program %x not found\n",
                  psi->id);
        return -1;
    }

    if(psi->version == mp->pmt_version)
        return 0;

    mp->pcr_pid = htob_16(unaligned16(dp + 8)) & 0x1fff;
    pi_len = htob_16(unaligned16(dp + 10)) & 0xfff;

    dp += 12;
    size -= 12;

    if(pi_len + 4 > size)
        return -1;

    tc2_print("MPEGTS", TC2_PRINT_DEBUG, "program %i\n", psi->id);
    tc2_print("MPEGTS", TC2_PRINT_DEBUG, "    PCR PID %x\n", mp->pcr_pid);
    tc2_print("MPEGTS", TC2_PRINT_DEBUG, "    program_info_length %d\n",
              pi_len);

    mpegts_free_program(mp);
    mp->pmt_version = psi->version;
    mp->program_info_length = pi_len;

    if(pi_len > 0){
        mp->descriptors = malloc(pi_len);
        if(!mp->descriptors)
            return -1;
        memcpy(mp->descriptors, dp, pi_len);
    }

    dp += pi_len;
    size -= pi_len;

    ns = size / 5;
    mp->streams = calloc(ns, sizeof(*mp->streams));
    if(!mp->streams)
        return -1;

    for(i = 0; i + 9 <= size; mp->num_streams++){
        mpegts_elem_stream_t *es = mp->streams + mp->num_streams;
        unsigned int stype, epid, esil;

        stype = dp[0];
        epid = htob_16(unaligned16(dp + 1)) & 0x1fff;
        esil = htob_16(unaligned16(dp + 3)) & 0xfff;
        dp += 5;
        i += 5;

        if(esil + i > size)
            return -1;

        es->stream_type = stype;
        es->pid = epid;
        es->es_info_length = esil;
        if(esil > 0){
            es->descriptors = malloc(esil);
            if(!es->descriptors)
                return -1;
            memcpy(es->descriptors, dp, esil);
        }

        dp += esil;
        i += esil;

        tc2_print("MPEGTS", TC2_PRINT_DEBUG, "    PID %x, type %x\n",
                  epid, stype);
    }

    if(i != size)
        return -1;

    mp->streams = realloc(mp->streams, mp->num_streams * sizeof(*mp->streams));

    return 0;
}

static void
mpegts_section_clear(mpegts_section_t *s)
{
    memset(s, 0, offsetof(mpegts_section_t, buf));
}

static mpegts_section_t *
mpegts_section_alloc(void)
{
    mpegts_section_t *s = malloc(sizeof(*s));
    if(s)
        mpegts_section_clear(s);
    return s;
}

static mpegts_section_t *
mpegts_section_get(mpegts_stream_t *s, unsigned int pid)
{
    unsigned int idx = MPEGTS_PID_INDEX(s->pidmap[pid]);
    mpegts_section_t *psi;
    mpegts_program_t *pg;

    if(idx == MPEGTS_PID_NO_INDEX){
        psi = s->psi;
        s->psi = NULL;
    } else {
        pg = s->programs + idx;
        psi = pg->psi;
        pg->psi = NULL;
    }

    return psi;
}

static int
mpegts_section_init(mpegts_section_t *psi)
{
    uint8_t *dp = psi->buf;
    unsigned int val;

    psi->table_id = *dp;

    val = htob_16(unaligned16(dp + 1));
    if((val & 0x8000) != 0x8000) /* section_syntax_indicator */
        return -1;
    if((val & 0x4000) != 0)      /* '0' */
        return -1;
    if((val & 0x3000) != 0x3000) /* reserved */
        return -1;

    psi->length = (val & 0xfff) + 3;
    if(psi->length > 1024){
        tc2_print("MPEGTS", TC2_PRINT_WARNING,
                  "invalid PSI section length %d\n", psi->length);
        return -1;
    }

    psi->id = htob_16(unaligned16(dp + 3));

    val = dp[5];
    if((val & 0xc0) != 0xc0)     /* reserved */
        return -1;

    psi->version = (val >> 1) & 0x1f;
    psi->current = val & 1;
    psi->number = dp[6];
    psi->last = dp[7];

    tc2_print("MPEGTS", TC2_PRINT_DEBUG,
              "PSI table_id=%x length=%d id=%x version=%d "
              "current=%d section=%d/%d\n",
              psi->table_id, psi->length, psi->id, psi->version,
              psi->current, psi->number, psi->last);

    return 0;
}

static int
mpegts_do_section(mpegts_stream_t *s, mpegts_section_t *psi, unsigned int pid)
{
    switch(psi->table_id){
    case MPEGTS_TABLE_ID_PAT:
        return mpegts_parse_pat(s, psi);
    case MPEGTS_TABLE_ID_PMT:
        return mpegts_parse_pmt(s, psi);
    }

    return 0;
}

static int
mpegts_do_psi(mpegts_stream_t *s, mpegts_packet_t *mp)
{
    mpegts_section_t *psi;
    unsigned int size;
    uint8_t *d;
    int err = 0;

    psi = mpegts_section_get(s, mp->pid);

    if(!psi && !mp->unit_start)
        return 0;

    d = mp->data;
    size = mp->data_length;

    if(mp->unit_start){
        unsigned int pointer = *d++;
        size--;

        if(!psi){
            d += pointer;
            size -= pointer;
        }
    }

    if(!psi)
        psi = mpegts_section_alloc();
    if(!psi)
        return -1;

    while(size){
        unsigned int len;

        if(psi->length){
            len = min(size, psi->length - psi->read);
        } else if(*d == 255){
            break;
        } else {
            len = min(size, MPEGTS_SECTION_COMMON_LEN);
        }

        if(psi->read + len > sizeof(psi->buf)){
            err = -1;
            goto out;
        }

        memcpy(psi->buf + psi->read, d, len);
        psi->read += len;
        d += len;
        size -= len;

        if(!psi->length && psi->read >= MPEGTS_SECTION_COMMON_LEN){
            if(mpegts_section_init(psi) < 0){
                err = -1;
                goto out;
            }
        }

        if(psi->length && psi->read == psi->length){
            mpegts_do_section(s, psi, mp->pid);
            mpegts_section_clear(psi);
        }
    }

    if(psi->length){
        if(psi->table_id == MPEGTS_TABLE_ID_PMT){
            mpegts_program_t *pg = mpegts_find_program(s, psi->id);
            if(pg){
                s->pidmap[mp->pid] =
                    MPEGTS_PID_MAP(MPEGTS_PID_TYPE_PSI, pg - s->programs);
                pg->psi = psi;
                psi = NULL;
            }
        } else {
            s->psi = psi;
            psi = NULL;
        }
    }

out:
    free(psi);
    return err;
}

static int
mpegts_do_packet(mpegts_stream_t *s, mpegts_packet_t *mp)
{
    unsigned int type = MPEGTS_PID_TYPE(s->pidmap[mp->pid]);

    switch(type){
    case MPEGTS_PID_TYPE_PSI:
        mpegts_do_psi(s, mp);
        break;
    }

    return 0;
}

static int
mpegts_num_pmt(mpegts_stream_t *s)
{
    unsigned int pmt_count = 0;
    unsigned int i;

    for(i = 0; i < s->num_programs; i++)
        if(s->programs[i].pmt_version != MPEGTS_PSI_NO_VERSION)
            pmt_count++;

    return pmt_count;
}

static int
mpegts_num_streams(mpegts_stream_t *s)
{
    unsigned int ns = 0;
    unsigned int i;

    for(i = 0; i < s->num_programs; i++)
        ns += s->programs[i].num_streams;

    return ns;
}

static int
mpegts_add_streams(muxed_stream_t *ms, mpegts_program_t *pg)
{
    mpegts_stream_t *s = ms->private;
    stream_t *sp = ms->streams + ms->n_streams;
    unsigned int i;

    tc2_print("MPEGTS", TC2_PRINT_DEBUG, "adding program %d [%x]\n",
              pg->program_number, pg->program_number);

    mpeg_parse_descriptors(ms, NULL, pg->descriptors, pg->program_info_length);

    for(i = 0; i < pg->num_streams; i++){
        mpegts_elem_stream_t *es = pg->streams + i;
        mpeg_stream_type_t *mst = mpeg_stream_type_id(es->stream_type);

        tc2_print("MPEGTS", TC2_PRINT_DEBUG, "    PID %x, type %x\n",
                  es->pid, es->stream_type);

        memset(sp, 0, sizeof(*sp));

        if(mst)
            sp->common.codec = mst->codec;

        mpeg_parse_descriptors(ms, sp, es->descriptors, es->es_info_length);

        if(sp->common.codec){
            if(!strncmp(sp->common.codec, "video/", 6))
                sp->stream_type = STREAM_TYPE_VIDEO;
            else if(!strncmp(sp->common.codec, "audio/", 6))
                sp->stream_type = STREAM_TYPE_AUDIO;
            else if(!strncmp(sp->common.codec, "subtitle/", 9))
                sp->stream_type = STREAM_TYPE_SUBTITLE;

            sp->common.index = ms->n_streams;
            sp->common.start_time = -1;
            sp->common.flags = TCVP_STREAM_FLAG_TRUNCATED;
            sp->common.program = pg->program_number;

            s->pidmap[es->pid] = MPEGTS_PID_MAP(MPEGTS_PID_TYPE_ES,
                                                ms->n_streams++);
            sp++;
        }
    }

    return 0;
}

extern muxed_stream_t *
mpegts_open(char *name, url_t *u, tcconf_section_t *cs, tcvp_timer_t *tm)
{
    muxed_stream_t *ms;
    mpegts_stream_t *s;
    mpegts_packet_t mp;
    unsigned int numpat = 0;
    unsigned int numpmt;
    unsigned int ns;
    int i;

    ms = tcallocdz(sizeof(*ms), NULL, mpegts_free);
    ms->next_packet = mpegts_packet;
    ms->seek = mpegts_seek;

    s = calloc(1, sizeof(*s));
    s->stream = tcref(u);
    s->tsbuf = malloc(2 * TS_PACKET_SIZE * TS_PACKET_BUF);
    s->tsp = s->tsbuf;
    s->end = -1;
    s->mp.pid = -1;
    s->pidmap = calloc((1 << 13), sizeof(*s->pidmap));
    s->pidmap[0] = MPEGTS_PID_PSI_NO_INDEX;
    s->pat_version = MPEGTS_PSI_NO_VERSION;

    ms->private = s;

    do {
        if(mpegts_read_packet(s, &mp) < 0)
            goto err;
        if(mpegts_do_packet(s, &mp) < 0)
            goto err;
        numpmt = mpegts_num_pmt(s);
        if(numpmt < 1)
            numpat = !!numpat;
        if(s->pat_version != MPEGTS_PSI_NO_VERSION && mp.pid == 0)
            numpat++;
    } while((!numpat || numpmt < s->num_programs) && numpat < 4);

    ns = mpegts_num_streams(s);
    ms->streams = calloc(ns, sizeof(*ms->streams));

    for(i = 0; i < s->num_programs; i++){
        if(mpegts_add_streams(ms, s->programs + i) < 0)
            goto err;
    }

    s->streams = calloc(ms->n_streams, sizeof(*s->streams));
    for(i = 0; i < ms->n_streams; i++){
        s->streams[i].buf = malloc(2 * MAX_PACKET_SIZE);
        s->streams[i].cc = -1;
    }

    s->start_time = -1LL;

    return ms;

  err:
    tcfree(ms);
    return NULL;
}

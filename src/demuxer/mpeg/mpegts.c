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

typedef struct mpegts_stream {
    url_t *stream;
    uint8_t *tsbuf, *tsp;
    int tsnbuf;
    int extra;
    int *imap;
    int pcrpid;
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
    continue;                                   \
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
    int sx = -1;
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
            sx = s->imap[mp.pid];

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
        } while(sx < 0 || !ms->used_streams[sx]);

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
mpegts_free(void *p)
{
    muxed_stream_t *ms = p;
    mpegts_stream_t *s = ms->private;
    int i;

    if(s->stream)
        s->stream->close(s->stream);
    if(s->imap)
        free(s->imap);
    if(s->streams){
        for(i = 0; i < ms->n_streams; i++)
            free(s->streams[i].buf);
        free(s->streams);
    }
    free(s->tsbuf);
    free(s);
    mpeg_free(ms);
}

static int
ispmt(int *pat, int np, mpegts_packet_t *mp){
    int i;
    for(i = 0; i < np; i++){
        if(pat[2*i+1] == mp->pid){
            int r = pat[2*i];
            if(mp->unit_start)
                pat[2*i] = -1;
            return r;
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
    int seclen, ptr;
    uint8_t *dp;
    int *pat = NULL;
    int i, n, ns, np;
    stream_t *sp;
    int pmtpid = 0;
    uint8_t *pmt = NULL;
    int pmtsize = 0;
    int pmtpos = 0;
    int program = -1;
    char *tmp, *p;
    int prog = -1;

    if((tmp = strchr(name, '?'))){
        tmp = strdup(++tmp);

        while((p = strsep(&tmp, "&"))){
            if(!*p)
                continue;
            if(!strncmp(p, "program=", 8)){
                prog = strtol(p + 8, NULL, 0);
            }
        }

        free(tmp);
    }
    ms = tcallocdz(sizeof(*ms), NULL, mpegts_free);
    ms->next_packet = mpegts_packet;
    ms->seek = mpegts_seek;

    s = calloc(1, sizeof(*s));
    s->stream = tcref(u);
    s->tsbuf = malloc(2 * TS_PACKET_SIZE * TS_PACKET_BUF);
    s->tsp = s->tsbuf;
    s->end = -1;
    s->mp.pid = -1;

    ms->private = s;

    do {
        if(mpegts_read_packet(s, &mp) < 0)
            goto err;
    } while(mp.pid != 0);

    if(!mp.unit_start){
        tc2_print("MPEGTS", TC2_PRINT_ERROR,
                  "BUG: Large PAT not supported.\n");
        goto err;
    }

    ptr = mp.data[0];
    if(ptr > mp.data_length - 4)
        goto err;
    dp = mp.data + ptr + 1;
    seclen = htob_16(unaligned16(dp + 1)) & 0xfff;
    if(seclen > mp.data_length - ptr - 4 || seclen < 9)
        goto err;
    if(mpeg_crc32(dp, seclen + 3)){
        tc2_print("MPEGTS", TC2_PRINT_WARNING, "Bad CRC in PAT.\n");
    }
    if(dp[6] || dp[7]){
        tc2_print("MPEGTS", TC2_PRINT_ERROR,
                  "BUG: Multi-section PAT not supported.\n");
        goto err;
    }

    n = (seclen - 9) / 4;
    pat = calloc(n, 2 * sizeof(*pat));
    dp += 8;
    for(i = 0; i < n; i++){
        int prg = htob_16(unaligned16(dp));
        int pid = htob_16(unaligned16(dp + 2)) & 0x1fff;

        tc2_print("MPEGTS", TC2_PRINT_DEBUG, "program %i => PMT pid %x\n",
                  prg, pid);

        pat[2*i] = 1;
        pat[2*i+1] = pid;
        dp += 4;

        if(prg == prog)
            pmtpid = pid;
    }

    if(pmtpid){
        tc2_print("MPEGTS", TC2_PRINT_DEBUG, "program %i [%x] selected\n",
                  prog, pmtpid);
    }

    np = ns = n;
    ms->streams = calloc(ns, sizeof(*ms->streams));
    s->imap = malloc((1 << 13) * sizeof(*s->imap));
    memset(s->imap, 0xff, (1 << 13) * sizeof(*s->imap));
    sp = ms->streams;

    while(program < 0){
        int pi_len, prg, ip = 0, pcrpid;
        uint32_t crc, ccrc;
        int dsize;

        do {
            if(mpegts_read_packet(s, &mp) < 0)
                goto err;
        } while((pmtpid && mp.pid != pmtpid) ||
                !(ip = ispmt(pat, np, &mp)));

        if(ip < 0 && !pmtpos)
            break;

        if(!pmtpos && !mp.unit_start)
            continue;

        if(mp.unit_start){
            if(mp.data[0] > mp.data_length - 4)
                goto err;

            dp = mp.data + mp.data[0] + 1;
            pmtsize = (htob_16(unaligned16(dp + 1)) & 0xfff) + 3;
            pmt = malloc(pmtsize);
            pmtpos = 0;
            pmtpid = mp.pid;
            dsize = min(pmtsize, mp.data_length - mp.data[0] - 1);

            tc2_print("MPEGTS", TC2_PRINT_DEBUG,
                      "reading PMT PID %x, size %#x\n", pmtpid, pmtsize);
        } else {
            dp = mp.data;
            dsize = min(pmtsize - pmtpos, mp.data_length);
        }

        tc2_print("MPEGTS", TC2_PRINT_DEBUG, "got %x bytes from PMT\n", dsize);
        memcpy(pmt + pmtpos, dp, dsize);
        pmtpos += dsize;

        if(pmtpos < pmtsize)
            continue;

        tc2_print("MPEGTS", TC2_PRINT_DEBUG, "got PMT, %x bytes\n", pmtpos);

        dp = pmt;
        seclen = htob_16(unaligned16(dp + 1)) & 0xfff;
        if(seclen > pmtsize - 3 || seclen < 13)
            goto err;
        crc = htob_32(unaligned32(dp + seclen - 1));

        if(crc && (ccrc = mpeg_crc32(dp, seclen + 3)) != 0){
            tc2_print("MPEGTS", TC2_PRINT_WARNING,
                      "Bad CRC in PMT, got %x, expected %x\n", ccrc, crc);
/*          continue; */
        }

        prg = htob_16(unaligned16(dp + 3));
        pcrpid = htob_16(unaligned16(dp + 8)) & 0x1fff;
        pi_len = htob_16(unaligned16(dp + 10)) & 0xfff;
        if(pi_len > seclen - 13)
            goto err;
        dp += 12;

        program = prg;
        s->pcrpid = pcrpid;

        tc2_print("MPEGTS", TC2_PRINT_DEBUG, "program %i\n", prg);
        tc2_print("MPEGTS", TC2_PRINT_DEBUG, "    PCR PID %x\n", pcrpid);

        for(i = 0; i < pi_len - 2;){
            int tag = dp[0];
            int tl = dp[1];

            tc2_print("MPEGTS", TC2_PRINT_DEBUG, "    descriptor %i\n", tag);
            dp += tl + 2;
            i += tl + 2;
        }

        if(i != pi_len)
            goto err;

        seclen -= 13 + pi_len;
        for(i = 0; i < seclen - 4;){
            mpeg_stream_type_t *mst;
            int stype, epid, esil;
            int j;

            if(ms->n_streams == ns){
                ns *= 2;
                ms->streams = realloc(ms->streams, ns * sizeof(*ms->streams));
                sp = &ms->streams[ms->n_streams];
            }

            memset(sp, 0, sizeof(*sp));

            stype = dp[0];
            epid = htob_16(unaligned16(dp + 1)) & 0x1fff;
            esil = htob_16(unaligned16(dp + 3)) & 0xfff;
            dp += 5;
            i += 5;

            if(esil > seclen - i)
                goto err;

            if((mst = mpeg_stream_type_id(stype)) != NULL){
                if(!strncmp(mst->codec, "video/", 6))
                    sp->stream_type = STREAM_TYPE_VIDEO;
                else if(!strncmp(mst->codec, "audio/", 6))
                    sp->stream_type = STREAM_TYPE_AUDIO;
                else if(!strncmp(mst->codec, "subtitle/", 9))
                    sp->stream_type = STREAM_TYPE_SUBTITLE;
                sp->common.codec = mst->codec;
                sp->common.index = ms->n_streams;
                sp->common.start_time = -1;
                sp->common.flags = TCVP_STREAM_FLAG_TRUNCATED;

                for(j = 0; j < esil;){
                    int tl = dp[1] + 2;
                    if(j + tl > esil)
                        goto err;
                    mpeg_descriptor(sp, dp);
                    dp += tl;
                    j += tl;
                }

                s->imap[epid] = ms->n_streams++;
                sp++;
            } else {
                dp += esil;
            }

            i += esil;

            tc2_print("MPEGTS", TC2_PRINT_DEBUG, "    PID %x, type %x\n",
                      epid, stype);
        }

        if(i != seclen)
            goto err;

        free(pmt);
        pmt = NULL;
        pmtpid = 0;
        pmtpos = 0;
        n--;
    }

    s->streams = calloc(ms->n_streams, sizeof(*s->streams));
    for(i = 0; i < ms->n_streams; i++){
        s->streams[i].buf = malloc(2 * MAX_PACKET_SIZE);
        s->streams[i].cc = -1;
    }

    s->start_time = -1LL;

  out:
    free(pat);
    free(pmt);
    return ms;

  err:
    tcfree(ms);
    ms = NULL;
    goto out;
}

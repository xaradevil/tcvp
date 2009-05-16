/**
    Copyright (C) 2003-2006  Michael Ahlberg, Måns Rullgård

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
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <pthread.h>
#include <tcalloc.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <tcvp_types.h>
#include <stream_tc2.h>

#ifdef HAVE_LIBMAGIC
#include <magic.h>

static magic_t file_magic;
static pthread_mutex_t magic_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

#define magic_size tcvp_demux_stream_conf_magic_size
#define suffix_map tcvp_demux_stream_conf_suffix
#define suffix_map_size tcvp_demux_stream_conf_suffix_count

static void
cpattr2(void *d, void *s, char *da, char *sa)
{
    if(!tcattr_get(d, da)){
        void *v = tcattr_get(s, sa);
        if(v){
            tcattr_set(d, da, strdup(v), NULL, free);
        }
    }
}

static void
cpattr(void *d, void *s, char *a)
{
    cpattr2(d, s, a, a);
}

static int
isplaylist(url_t *u, char *mime, char *name)
{
    char buf[1024], buf1[1024];
    struct stat st;
    uint64_t pos;
    char *p, *q;

    if(strcmp(mime, "text/plain"))
        return 0;

    pos = u->tell(u);
    p = url_gets(buf, sizeof(buf), u);
    u->seek(u, pos, SEEK_SET);
    if(!p)
        return 0;

    p = strchr(buf, ':');
    if(p){
        for(q = buf; q < p; q++){
            if(*q >= 'a' && *q <= 'z')
                continue;
            if(isdigit(*q))
                continue;
            if(*q == '-' || *q == '+' || *q == '.')
                continue;
            break;
        }
        if(p == q)
            return 1;
    }

    p = strchr(buf, 0);
    if(p > buf && *--p == '\n')
        *p = 0;
    if(p > buf && *--p == '\r')
        *p = 0;

    if(buf[0] != '/' && name){
        strncpy(buf1, name, sizeof(buf1));
        p = strrchr(buf1, '/');
        if(p)
            p++;
        else
            p = buf1;
        strncpy(p, buf, sizeof(buf1) - (p - buf1));
        p = buf1;
    } else {
        p = buf;
    }

    if(stat(p, &st))
        return 0;
    return 1;
}

static char *
s_magic_suffix(char *name)
{
    char *s = strrchr(name, '.');
    char *m = NULL;

    if(s){
        int i;
        for(i = 0; i < suffix_map_size; i++){
            if(!strcmp(s, suffix_map[i].suffix)){
                if(suffix_map[i].demuxer)
                    m = strdup(suffix_map[i].demuxer);
                break;
            }
        }
    }

    return m;
}

extern char *
s_magic(url_t *u, char *name)
{
    char buf[magic_size];
    const char *mg;
    char *m = NULL;
    uint64_t pos;
    int mgs;

    tc2_print("STREAM", TC2_PRINT_DEBUG, "s_magic %s\n", name);

#ifdef HAVE_LIBMAGIC
    pos = u->tell(u);
    mgs = u->read(buf, 1, magic_size, u);
    u->seek(u, pos, SEEK_SET);
    if(mgs < magic_size)
        return NULL;
    pthread_mutex_lock(&magic_lock);
    mg = magic_buffer(file_magic, buf, mgs);
    if(mg){
        int e;
        m = strdup(mg);
        e = strcspn(m, " \t;");
        m[e] = 0;
        if(isplaylist(u, m, name)){
            free(m);
            m = strdup("application/x-playlist");
        } else if(!strcmp(m, "data") ||
                  !strcmp(m, "application/octet-stream")){
            free(m);
            m = NULL;
        }
    }
    pthread_mutex_unlock(&magic_lock);
#endif

    if(!m && name)
        m = s_magic_suffix(name);

    tc2_print("STREAM", TC2_PRINT_DEBUG, "  type %s\n", m);

    return m;
}


extern char *
s_magic_url(char *url)
{
    url_t *u;
    char *m;

    u = url_open(url, "r");
    if(!u)
        return NULL;
    m = s_magic(u, url);
    tcfree(u);

    return m;
}

extern muxed_stream_t *
s_open(char *name, tcconf_section_t *cs, tcvp_timer_t *t)
{
    char *m;
    url_t *u;
    demux_open_t sopen;
    muxed_stream_t *ms;

    if(!(u = url_open(name, "r")))
        return NULL;

    m = s_magic(u, name);

    tc2_print("STREAM", TC2_PRINT_DEBUG, "mime type %s\n", m);

    if(m && strncmp(m, "audio/", 6) && strncmp(m, "video/", 6)){
        free(m);
        m = NULL;
    }

    if(!m)
        return NULL;

    if(!(sopen = tc2_get_symbol(m, "open")))
        return NULL;

    free(m);

    ms = sopen(name, u, cs, t);
    if(ms){
        char *a, *p;

        tcattr_set(ms, "file", strdup(name), NULL, free);
        cpattr(ms, u, "title");
        cpattr(ms, u, "performer");
        cpattr(ms, u, "artist");
        cpattr(ms, u, "album");
        cpattr(ms, u, "track");
        cpattr(ms, u, "year");
        cpattr(ms, u, "genre");

        cpattr2(ms, u, "title", "user.title");
        cpattr2(ms, u, "artist", "user.artist");
        cpattr2(ms, u, "album", "user.album");
        cpattr2(ms, u, "track", "user.track");
        cpattr2(ms, u, "year", "user.year");
        cpattr2(ms, u, "genre", "user.genre");

        a = tcattr_get(ms, "artist");
        p = tcattr_get(ms, "performer");

        if(!a && p)
            tcattr_set(ms, "artist", strdup(p), NULL, free);
        if(a && !p)
            tcattr_set(ms, "performer", strdup(a), NULL, free);
    }

    tcfree(u);

    return ms;
}

extern tcvp_packet_t *
s_next_packet(muxed_stream_t *ms, int stream)
{
    return ms->next_packet(ms, stream);
}

extern int
s_validate(char *name, tcconf_section_t *cs)
{
    muxed_stream_t *ms = s_open(name, cs, NULL);
    tcvp_packet_t *pk;
    int i;

    if(!ms)
        return -1;

    for(i = 0; i < ms->n_streams; i++)
        ms->used_streams[i] = 1;

    while((pk = ms->next_packet(ms, -1)))
        tcfree(pk);

    tcfree(ms);
    return 0;
}

extern tcvp_pipe_t *
s_open_mux(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
           muxed_stream_t *ms)
{
    mux_new_t mnew = NULL;
    char *name, *sf;
    char *m = NULL;

    if(tcconf_getvalue(cs, "mux/url", "%s", &name) <= 0)
        return NULL;

    if((sf = strrchr(name, '.'))){
        int i;
        for(i = 0; i < suffix_map_size; i++){
            if(!strcmp(sf, suffix_map[i].suffix)){
                m = suffix_map[i].muxer;
                break;
            }
        }
    }

    if(m){
        char mb[strlen(m) + 5];
        sprintf(mb, "mux/%s", m);
        mnew = tc2_get_symbol(mb, "new");
    }

    free(name);
    return mnew? mnew(s, cs, t, ms): NULL;
}

extern int
s_init(char *p)
{
#ifdef HAVE_LIBMAGIC
    file_magic = magic_open(MAGIC_MIME);
    magic_load(file_magic, DATADIR "/magic");
#endif

    return 0;
}

extern int
s_shdn(void)
{
#ifdef HAVE_LIBMAGIC
    magic_close(file_magic);
#endif
    return 0;
}

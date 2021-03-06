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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <pthread.h>
#include <tclist.h>
#include <signal.h>
#include <tcdirent.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/rand.h>
#include <tcvp_types.h>
#include <remote_tc2.h>

#define COOKIE_SIZE 8

typedef struct tcvp_remote {
    eventq_t qr, sc, ss, st;
    pthread_t eth, lth;
    int ssock;
    tcconf_section_t *conf;
    tclist_t *clients;
    fd_set clf;
    int run;
    u_char cookie[COOKIE_SIZE];
} tcvp_remote_t;

typedef struct tcvp_remote_client {
    int auth;
    int socket;
    struct sockaddr_in addr;
} tcvp_remote_client_t;

#define CONTROL 1
#define STATUS  2
#define TIMER   3

static int *event_types;
static int max_event;

static int
get_cookie(u_char *cookie)
{
    FILE *cf;
    char *home;
    char *ck;
    int ret = 0;

    if(!(home = getenv("HOME")))
        return -1;

    ck = alloca(strlen(home) + 22);
    sprintf(ck, "%s/.tcvp/remote", home);
    if(tcmkpath(ck, 0755))
        return -1;

    strcat(ck, "/cookie");
    if((cf = fopen(ck, "r"))){
        if(fread(cookie, 1, COOKIE_SIZE, cf) != COOKIE_SIZE)
            ret = -1;
        fclose(cf);
    } else if((cf = fopen(ck, "w"))){
        fchmod(fileno(cf), S_IRUSR | S_IWUSR);
        RAND_pseudo_bytes(cookie, COOKIE_SIZE);
        if(fwrite(cookie, 1, COOKIE_SIZE, cf) != COOKIE_SIZE)
            ret = -1;
        fclose(cf);
    } else {
        ret = -1;
    }

    return ret;
}

static void
cl_free(void *p)
{
    tcvp_remote_client_t *cl = p;

    close(cl->socket);
    free(cl);
}

static void *
rm_event(void *p)
{
    tcvp_remote_t *rm = p;
    int run = 1;

    while(run){
        tcvp_event_t *te = eventq_recv(rm->qr);
        u_char *se;
        int size;

        if(te->type == -1){
            run = 0;
            tcfree(te);
            break;
        }

        if(tcattr_get(te, "addr")){
            tcfree(te);
            continue;
        }

        se = tcvp_event_serialize(te, &size);
        if(se){
            tclist_item_t *li = NULL;
            uint32_t s = htonl(size);
            tcvp_remote_client_t *cl;

/*          fprintf(stderr, "REMOTE: sending %s, %i bytes\n", se, size); */
            while((cl = tclist_next(rm->clients, &li))){
                if(!cl->auth)
                    continue;
                if(send(cl->socket, &s, 4, MSG_MORE | MSG_NOSIGNAL) < 0 ||
                   send(cl->socket, se, size, MSG_NOSIGNAL) < 0){
                    tc2_print("REMOTE", TC2_PRINT_ERROR,
                              "error sending %s\n", se);
                    FD_CLR(cl->socket, &rm->clf);
                    tclist_remove(rm->clients, li, cl_free);
                }
            }
            free(se);
        }

        tcfree(te);
    }

    return NULL;
}

static int
read_event(tcvp_remote_t *rm, tcvp_remote_client_t *cl)
{
    uint32_t size;
    u_char *buf, *p;
    int s;
    tcvp_event_t *te;
    int ret = 0;

    if(read(cl->socket, &size, 4) < 4)
        return -1;
    size = ntohl(size);

    buf = malloc(size);
    p = buf;
    s = size;

    while(s > 0){
        int r = read(cl->socket, p, s);
        if(r < 0){
            ret = -1;
            goto end;
        }
        s -= r;
        p += r;
    }

    tc2_print("REMOTE", TC2_PRINT_DEBUG, "received %s\n", buf);

    te = tcvp_event_deserialize(buf, size);
    if(te){
        tcattr_set(te, "addr", &cl->addr, NULL, NULL);
        if(te->type > max_event || !event_types[te->type]){
            tc2_print("REMOTE", TC2_PRINT_WARNING,
                      "received unknown event %s\n", buf);
        } else if(event_types[te->type] == CONTROL){
            eventq_send(rm->sc, te);
        } else if(event_types[te->type] == STATUS){
            eventq_send(rm->ss, te);
        } else if(event_types[te->type] == TIMER){
            eventq_send(rm->st, te);
        }
        tcfree(te);
    }

end:
    free(buf);
    return ret;
}

static int
send_features(tcvp_remote_t *rm, tcvp_remote_client_t *cl)
{
    void *s = NULL;
    char *f, l;

    while(tcconf_nextvalue_g(rm->conf, "features/*", &s, &f, "") >= 0 && s){
        tc2_print("REMOTE", TC2_PRINT_DEBUG, "send feature %s\n", f);
        l = strlen(f);
        send(cl->socket, &l, 1, MSG_NOSIGNAL | MSG_MORE);
        send(cl->socket, f, l, MSG_NOSIGNAL | MSG_MORE);
    }

    l = 0;
    send(cl->socket, &l, 1, MSG_NOSIGNAL | MSG_MORE);

    return 0;
}

static int
recv_features(tcvp_remote_t *rm, tcvp_remote_client_t *cl)
{
    char fbuf[512];
    char *f;
    u_char l;

    f = fbuf + sprintf(fbuf, "features/");

    while(recv(cl->socket, &l, 1, MSG_NOSIGNAL) == 1 && l > 0){
        recv(cl->socket, f, l, MSG_NOSIGNAL);
        f[l] = 0;
        tcconf_setvalue(rm->conf, fbuf, "");
    }

    return 0;
}

static void *
rm_listen(void *p)
{
    tcvp_remote_t *rm = p;

    while(rm->run){
        tcvp_remote_client_t *cl;
        tclist_item_t *li = NULL;
        struct timeval tv = { 1, 0 };
        fd_set tmp = rm->clf;

        if(select(FD_SETSIZE, &tmp, NULL, NULL, &tv) <= 0)
            continue;

        if(rm->ssock >= 0 && FD_ISSET(rm->ssock, &tmp)){
            struct sockaddr_in sa;
            socklen_t sl = sizeof(sa);
            int s;

            s = accept(rm->ssock, (struct sockaddr *) &sa, &sl);
            if(s < 0)
                continue;
/*          fprintf(stderr, "REMOTE: connect from %s\n", */
/*                  inet_ntoa(sa.sin_addr)); */
            cl = calloc(1, sizeof(*cl));
            cl->socket = s;
            cl->addr = sa;
            FD_SET(s, &rm->clf);
            tclist_push(rm->clients, cl);
        }

        while((cl = tclist_next(rm->clients, &li))){
            if(FD_ISSET(cl->socket, &tmp)){
                if(cl->auth){
                    if(read_event(rm, cl) < 0){
                        FD_CLR(cl->socket, &rm->clf);
                        tclist_remove(rm->clients, li, cl_free);
                    }
                } else {
                    u_char cookie[COOKIE_SIZE];
                    if(read(cl->socket, cookie, COOKIE_SIZE) == COOKIE_SIZE &&
                       !memcmp(cookie, rm->cookie, COOKIE_SIZE)){
                        cl->auth = 1;
                        write(cl->socket, "auth", 4);
                        send_features(rm, cl);
                    } else {
                        FD_CLR(cl->socket, &rm->clf);
                        tclist_remove(rm->clients, li, cl_free);
                    }
                }
            }
        }
    }

    return NULL;
}

static void
free_cl(void *p)
{
    tcvp_remote_client_t *cl = p;
    close(cl->socket);
    free(cl);
}

static void
rm_free(void *p)
{
    tcvp_module_t *ad = p;
    tcvp_remote_t *rm = ad->private;

    if(rm->qr){
        tcvp_event_send(rm->qr, -1);
        pthread_join(rm->eth, NULL);
        eventq_delete(rm->qr);
    }

    rm->run = 0;
    pthread_join(rm->lth, NULL);

    if(rm->ssock >= 0)
        close(rm->ssock);

    tclist_destroy(rm->clients, free_cl);

    if(rm->ss)
        eventq_delete(rm->ss);
    if(rm->sc)
        eventq_delete(rm->sc);
    if(rm->st)
        eventq_delete(rm->st);
    if(rm->conf)
        tcfree(rm->conf);
    free(rm);
}

static int
rm_start(tcvp_module_t *ad)
{
    tcvp_remote_t *rm = ad->private;

    rm->sc = tcvp_event_get_sendq(rm->conf, "control");
    rm->ss = tcvp_event_get_sendq(rm->conf, "status");
    rm->st = tcvp_event_get_sendq(rm->conf, "timer");
    rm->qr = tcvp_event_get_recvq(rm->conf, "control", "status", "timer",NULL);

    signal(SIGPIPE, SIG_IGN);
    rm->run = 1;
    pthread_create(&rm->eth, NULL, rm_event, rm);
    pthread_create(&rm->lth, NULL, rm_listen, rm);
    return 0;
}

extern tcvp_module_t *
rm_new(tcconf_section_t *cs)
{
    tcvp_remote_t *rm;
    tcvp_module_t *ad;
    int sock;
    int port = tcvp_remote_conf_port;
    struct sockaddr_in rsa;

    tcconf_getvalue(cs, "remote/port", "%i", &port);
    port = htons(port);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
        return NULL;

    rsa.sin_family = AF_INET;
    rsa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    rsa.sin_port = port;

    rm = calloc(1, sizeof(*rm));
    rm->conf = tcref(cs);
    rm->clients = tclist_new(TC_LOCK_SLOPPY);
    FD_ZERO(&rm->clf);
    if(get_cookie(rm->cookie))
        return NULL;

    if(!connect(sock, (struct sockaddr *) &rsa, sizeof(rsa))){
        tcvp_remote_client_t *cl;
        char buf[64];

        write(sock, rm->cookie, COOKIE_SIZE);
        if(read(sock, buf, 4) != 4 || memcmp(buf, "auth", 4))
            return NULL;

        FD_SET(sock, &rm->clf);
        cl = calloc(1, sizeof(*cl));
        cl->socket = sock;
        cl->addr = rsa;
        cl->auth = 1;
        recv_features(rm, cl);
        tclist_push(rm->clients, cl);
        rm->ssock = -1;
    } else {
        int r = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &r, sizeof(r));
        if(bind(sock, (struct sockaddr *) &rsa, sizeof(rsa)) < 0){
            tc2_print("REMOTE", TC2_PRINT_ERROR, "bind failed: %m\n");
            return NULL;
        }
        listen(sock, 16);
        FD_SET(sock, &rm->clf);
        rm->ssock = sock;
    }

    ad = tcallocdz(sizeof(*ad), NULL, rm_free);
    ad->init = rm_start;
    ad->private = rm;

    return ad;
}

static void
get_event(char *evt, int type)
{
    int e = tcvp_event_get(evt);
    if(e > max_event){
        event_types = realloc(event_types, (e + 1) * sizeof(*event_types));
        memset(event_types + max_event + 1, 0,
               (e - max_event) * sizeof(*event_types));
        max_event = e;
    }
    event_types[e] = type;
}

extern int
rm_init(char *p)
{
    get_event("TCVP_PL_START", CONTROL);
    get_event("TCVP_PL_STOP", CONTROL);
    get_event("TCVP_PL_NEXT", CONTROL);
    get_event("TCVP_PL_PREV", CONTROL);
    get_event("TCVP_PL_ADD", CONTROL);
    get_event("TCVP_PL_ADDLIST", CONTROL);
    get_event("TCVP_PL_REMOVE", CONTROL);
    get_event("TCVP_PL_FLAGS", CONTROL);
    get_event("TCVP_PL_STATE", STATUS);
    get_event("TCVP_PL_CONTENT", STATUS);
    get_event("TCVP_PL_SEEK", CONTROL);
    get_event("TCVP_DB_QUERY", CONTROL);
    get_event("TCVP_DB_REPLY", CONTROL);
    get_event("TCVP_KEY", CONTROL);
    get_event("TCVP_STATE", STATUS);
    get_event("TCVP_OPEN", CONTROL);
    get_event("TCVP_START", CONTROL);
    get_event("TCVP_PAUSE", CONTROL);
    get_event("TCVP_STOP", CONTROL);
    get_event("TCVP_CLOSE", CONTROL);
    get_event("TCVP_SEEK", CONTROL);
    get_event("TCVP_TIMER", TIMER);
    get_event("TCVP_LOAD", STATUS);
    get_event("TCVP_QUERY", CONTROL);
    get_event("TCVP_STREAM_INFO", STATUS);

    return 0;
}

extern int
rm_shdn(void)
{
    free(event_types);
    return 0;
}

/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <pthread.h>
#include <tclist.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <tcvp_types.h>
#include <remote_tc2.h>

typedef struct tcvp_remote {
    eventq_t qr, sc, ss, st;
    pthread_t eth, lth;
    int ssock;
    tcconf_section_t *conf;
    list *clients;
    fd_set clf;
    int run;
} tcvp_remote_t;

typedef struct tcvp_remote_client {
    int socket;
    struct sockaddr_in addr;
} tcvp_remote_client_t;

#define CONTROL 1
#define STATUS  2
#define TIMER   3

static int *event_types;
static int max_event;

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
	    continue;
	}

	if(tcattr_get(te, "addr"))
	    continue;

	se = tcvp_event_serialize(te, &size);
	if(se){
	    list_item *li = NULL;
	    uint32_t s = htonl(size);
	    tcvp_remote_client_t *cl;

/* 	    fprintf(stderr, "REMOTE: serialized %i as %i bytes\n", */
/* 		    te->type, size); */
	    while((cl = list_next(rm->clients, &li))){
		if(write(cl->socket, &s, 4) < 0 ||
		   write(cl->socket, se, size) < 0){
		    list_remove(rm->clients, li);
		    FD_CLR(cl->socket, &rm->clf);
		    close(cl->socket);
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

/*     fprintf(stderr, "REMOTE: received %s\n", buf); */

    te = tcvp_event_deserialize(buf, size);
    if(te){
	tcattr_set(te, "addr", &cl->addr, NULL, NULL);
	if(te->type > max_event || !event_types[te->type]){
	    fprintf(stderr, "REMOTE: don't know what to do with %s\n", buf);
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

static void *
rm_listen(void *p)
{
    tcvp_remote_t *rm = p;

    while(rm->run){
	tcvp_remote_client_t *cl;
	list_item *li = NULL;
	struct timeval tv = { 1, 0 };
	fd_set tmp = rm->clf;

	if(select(FD_SETSIZE, &tmp, NULL, NULL, &tv) <= 0)
	    continue;

	if(rm->ssock >= 0 && FD_ISSET(rm->ssock, &tmp)){
	    struct sockaddr_in sa;
	    int sl = sizeof(sa);
	    int s;

	    s = accept(rm->ssock, (struct sockaddr *) &sa, &sl);
	    if(s < 0)
		continue;
/* 	    fprintf(stderr, "REMOTE: connect from %s\n", */
/* 		    inet_ntoa(sa.sin_addr)); */
	    cl = malloc(sizeof(*cl));
	    cl->socket = s;
	    cl->addr = sa;
	    FD_SET(s, &rm->clf);
	    list_push(rm->clients, cl);
	}

	while((cl = list_next(rm->clients, &li))){
	    if(FD_ISSET(cl->socket, &tmp)){
		if(read_event(rm, cl) < 0){
/* 		    fprintf(stderr, "REMOTE: read event failed\n"); */
		    list_remove(rm->clients, li);
		    FD_CLR(cl->socket, &rm->clf);
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
    tcvp_addon_t *ad = p;
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

    list_destroy(rm->clients, free_cl);

    if(rm->ss)
	eventq_delete(rm->ss);
    if(rm->sc)
	eventq_delete(rm->sc);
    if(rm->st)
	eventq_delete(rm->st);
    if(rm->conf)
	tcfree(rm->conf);
}

static int
rm_start(tcvp_addon_t *ad)
{
    tcvp_remote_t *rm = ad->private;
    char *qname, *qn;

    tcconf_getvalue(rm->conf, "qname", "%s", &qname);
    qn = alloca(strlen(qname) + 9);

    rm->qr = eventq_new(tcref);
    rm->ss = eventq_new(NULL);
    rm->sc = eventq_new(NULL);
    rm->st = eventq_new(NULL);

    sprintf(qn, "%s/control", qname);
    eventq_attach(rm->qr, qn, EVENTQ_RECV);
    eventq_attach(rm->sc, qn, EVENTQ_SEND);

    sprintf(qn, "%s/status", qname);
    eventq_attach(rm->qr, qn, EVENTQ_RECV);
    eventq_attach(rm->ss, qn, EVENTQ_SEND);

    sprintf(qn, "%s/timer", qname);
    eventq_attach(rm->qr, qn, EVENTQ_RECV);
    eventq_attach(rm->st, qn, EVENTQ_SEND);

    free(qname);

    signal(SIGPIPE, SIG_IGN);
    rm->run = 1;
    pthread_create(&rm->eth, NULL, rm_event, rm);
    pthread_create(&rm->lth, NULL, rm_listen, rm);
    return 0;
}

static int qnum;

extern tcvp_addon_t *
rm_new(tcconf_section_t *cs)
{
    tcvp_remote_t *rm;
    tcvp_addon_t *ad;
    int sock;
    int port = htons(tcvp_addon_remote_conf_port);
    struct sockaddr_in rsa;

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if(sock < 0)
	return NULL;

    rsa.sin_family = PF_INET;
    rsa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    rsa.sin_port = port;

    rm = calloc(1, sizeof(*rm));
    rm->conf = tcref(cs);
    rm->clients = list_new(TC_LOCK_SLOPPY);
    FD_ZERO(&rm->clf);

    if(!connect(sock, (struct sockaddr *) &rsa, sizeof(rsa))){
	tcvp_remote_client_t *cl;
	char buf[64];
	sprintf(buf, "TCVP/remote-%i", qnum++);
	tcconf_setvalue(cs, "qname", "%s", buf);
	FD_SET(sock, &rm->clf);
	cl = calloc(1, sizeof(*cl));
	cl->socket = sock;
	cl->addr = rsa;
	list_push(rm->clients, cl);
	rm->ssock = -1;
    } else {
	int r = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &r, sizeof(r));
	if(bind(sock, (struct sockaddr *) &rsa, sizeof(rsa)) < 0){
	    fprintf(stderr, "REMOTE: bind failed: %m\n");
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
	memset(event_types + e, 0, (e - max_event) * sizeof(*event_types));
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
    get_event("TCVP_PL_SHUFFLE", CONTROL);
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

    return 0;
}

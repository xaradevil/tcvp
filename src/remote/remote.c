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
    eventq_t qr, sc, ss;
    pthread_t eth, lth;
    int ssock, rsock;
    tcconf_section_t *conf;
    list *clients;
    int run;
} tcvp_remote_t;

typedef struct tcvp_remote_client {
    int socket;
    struct sockaddr_in addr;
} tcvp_remote_client_t;

static int TCVP_STATE;
static int TCVP_OPEN;
static int TCVP_START;
static int TCVP_CLOSE;
static int TCVP_PL_START;
static int TCVP_PL_STOP;
static int TCVP_PL_NEXT;
static int TCVP_PL_PREV;
static int TCVP_PL_ADD;
static int TCVP_PL_ADDLIST;
static int TCVP_PL_REMOVE;
static int TCVP_PL_SHUFFLE;


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

	se = tcvp_event_serialize(te, &size);
	if(se){
	    list_item *li = NULL;
	    uint32_t s = htonl(size);
	    tcvp_remote_client_t *cl;

/* 	    fprintf(stderr, "REMOTE: serialized %i as %i bytes\n", */
/* 		    te->type, size); */
	    while((cl = list_next(rm->clients, &li))){
		if(tcattr_get(te, "addr")){
		    continue;
		}
		if(write(cl->socket, &s, 4) < 0 ||
		   write(cl->socket, se, size) < 0){
		    list_remove(rm->clients, li);
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

    if(read(cl->socket, &size, 4) < 4)
	return -1;
    size = ntohl(size);

    buf = malloc(size);
    p = buf;
    s = size;

    while(s > 0){
	int r = read(cl->socket, p, s);
	if(r < 0)
	    return -1;
	s -= r;
	p += r;
    }

    te = tcvp_event_deserialize(buf, size);
    if(te){
	tcattr_set(te, "addr", &cl->addr, NULL, NULL);
	eventq_send(rm->sc, te);
	tcfree(te);
    }

    fprintf(stderr, "REMOTE: received %s\n", buf);
    return 0;
}

static void *
rm_listen(void *p)
{
    tcvp_remote_t *rm = p;
    fd_set clf;

    FD_ZERO(&clf);
    if(rm->ssock >= 0)
	FD_SET(rm->ssock, &clf);
    if(rm->rsock >= 0)
	FD_SET(rm->rsock, &clf);

    while(rm->run){
	tcvp_remote_client_t *cl;
	list_item *li = NULL;
	struct timeval tv = { 1, 0 };
	fd_set tmp = clf;

	if(select(FD_SETSIZE, &tmp, NULL, NULL, &tv) <= 0)
	    continue;

	if(rm->ssock >= 0 && FD_ISSET(rm->ssock, &tmp)){
	    struct sockaddr_in sa;
	    int sl = sizeof(sa);
	    char buf[256];
	    int s;

	    s = accept(rm->ssock, (struct sockaddr *) &sa, &sl);
	    if(s < 0)
		continue;
	    fprintf(stderr, "REMOTE: connect from %s\n",
		    inet_ntop(sa.sin_family, &sa.sin_addr, buf, sizeof(buf)));
	    cl = malloc(sizeof(*cl));
	    cl->socket = s;
	    cl->addr = sa;
	    FD_SET(s, &clf);
	    list_push(rm->clients, cl);
	}

	while((cl = list_next(rm->clients, &li))){
	    if(FD_ISSET(cl->socket, &tmp))
		if(read_event(rm, cl) < 0){
		    fprintf(stderr, "REMOTE: read event failed\n");
		    list_remove(rm->clients, li);
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

    sprintf(qn, "%s/control", qname);
    eventq_attach(rm->qr, qn, EVENTQ_RECV);
    eventq_attach(rm->sc, qn, EVENTQ_SEND);

    sprintf(qn, "%s/status", qname);
    eventq_attach(rm->qr, qn, EVENTQ_RECV);
    eventq_attach(rm->ss, qn, EVENTQ_SEND);

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
    
    if(!connect(sock, (struct sockaddr *) &rsa, sizeof(rsa))){
	tcvp_remote_client_t *cl;
	char buf[64];
	sprintf(buf, "TCVP/remote-%i", qnum++);
	tcconf_setvalue(cs, "qname", "%s", buf);
	rm->rsock = sock;
	cl = calloc(1, sizeof(*cl));
	cl->socket = sock;
	cl->addr = rsa;
	list_push(rm->clients, cl);
	rm->ssock = -1;
    } else {
	if(bind(sock, (struct sockaddr *) &rsa, sizeof(rsa)) < 0){
	    fprintf(stderr, "REMOTE: bind failed: %m\n");
	    return NULL;
	}
	listen(sock, 16);
	rm->ssock = sock;
	rm->rsock = -1;
    }

    ad = tcallocdz(sizeof(*ad), NULL, rm_free);
    ad->init = rm_start;
    ad->private = rm;

    return ad;
}

extern int
rm_init(char *p)
{
    TCVP_PL_START = tcvp_event_get("TCVP_PL_START");
    TCVP_PL_STOP = tcvp_event_get("TCVP_PL_STOP");
    TCVP_PL_NEXT = tcvp_event_get("TCVP_PL_NEXT");
    TCVP_PL_PREV = tcvp_event_get("TCVP_PL_PREV");
    TCVP_PL_ADD = tcvp_event_get("TCVP_PL_ADD");
    TCVP_PL_ADDLIST = tcvp_event_get("TCVP_PL_ADDLIST");
    TCVP_PL_REMOVE = tcvp_event_get("TCVP_PL_REMOVE");
    TCVP_PL_SHUFFLE = tcvp_event_get("TCVP_PL_SHUFFLE");
    TCVP_STATE = tcvp_event_get("TCVP_STATE");
    TCVP_OPEN = tcvp_event_get("TCVP_OPEN"); 
    TCVP_START = tcvp_event_get("TCVP_START");
    TCVP_CLOSE = tcvp_event_get("TCVP_CLOSE");

    return 0;
}

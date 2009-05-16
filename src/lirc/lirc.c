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

#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <lirc_tc2.h>
#include <lirc/lirc_client.h>

typedef struct tcvplirc {
    tcconf_section_t *conf;
    eventq_t qs;
    pthread_t th;
    int sock;
    struct lirc_config *lconfig;
    int quit;
} tcvplirc_t;

static void *
tlirc_run(void *p)
{
    char *code;
    char *c;
    int ret;
    tcvplirc_t *tl = (tcvplirc_t *)p;
    int selret;
    fd_set active_fd_set, read_fd_set;
    struct timeval tv;

    FD_ZERO (&active_fd_set);
    FD_SET (tl->sock, &active_fd_set);

    while(1) {
        read_fd_set = active_fd_set;
        tv.tv_usec = 500000;
        tv.tv_sec = 0;
        selret = select (FD_SETSIZE, &read_fd_set, NULL, NULL, &tv);

        if(selret < 0 || tl->quit != 0) {
            return NULL;
        }

        if(selret > 0)
        {
            lirc_nextcode(&code);
            if(code==NULL) continue;
            while((ret=lirc_code2char(tl->lconfig, code, &c))==0 &&
                  c!=NULL)
            {
                tc2_print("lirc", TC2_PRINT_DEBUG, "Command \"%s\"\n", c);
                tcvp_event_send(tl->qs, TCVP_KEY, c);
            }
            free(code);
            if(ret==-1) break;
        }
    }

    return NULL;
}

extern int
tlirc_init(tcvp_module_t *tm)
{
    tcvplirc_t *tl = tm->private;
    int flags;

    tl->qs = tcvp_event_get_sendq(tl->conf, "control");

    tl->sock = lirc_init("tcvp", 0);
    if(tl->sock < 0) {
        tc2_print("lirc", TC2_PRINT_WARNING, "failed to initialize lirc\n");
        return -1;
    }

    if(lirc_readconfig(NULL, &tl->lconfig, NULL) != 0) {
        tc2_print("lirc", TC2_PRINT_WARNING, "failed to read lirc config\n");
        lirc_deinit();
        return -1;
    }

    fcntl(tl->sock,F_SETOWN,getpid());
    flags=fcntl(tl->sock,F_GETFL,0);
    if(flags!=-1)
    {
        fcntl(tl->sock,F_SETFL,flags|O_NONBLOCK);
    }

    pthread_create(&tl->th, NULL, tlirc_run, tl);

    return 0;
}

extern void
tlirc_free(void *p)
{
    tcvplirc_t *tl = p;

    eventq_delete(tl->qs);

    if(tl->th) {
        tl->quit = 1;
        pthread_join(tl->th, NULL);
    }

    tcfree(tl->conf);

    lirc_freeconfig(tl->lconfig);
    lirc_deinit();
}

extern int
tlirc_new(tcvp_module_t *m, tcconf_section_t *c)
{
    tcvplirc_t *tl = tcallocdz(sizeof(*tl), NULL, tlirc_free);
    tl->conf = tcref(c);

    m->private = tl;

    return 0;
}

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
#include <cdda_tc2.h>
#include <cdda_interface.h>
#include <cdda_paranoia.h>
#include <tcstring.h>
#include <tcalloc.h>
#include <cdda_mod.h>

static char *
cddb_cmd(char *cmd)
{
    char url[1024];
    char *reply;
    int rsize;
    url_t *u;
    char *p;
    int n;

    p = cmd = strdup(cmd);
    while(*p){
	*p = *p == ' '? '+': *p;
	p++;
    }

    snprintf(url, sizeof(url), "http://%s/~cddb/cddb.cgi?"
	     "cmd=%s&hello=%s+%s+%s+%s&proto=6",
	     tcvp_input_cdda_conf_cddb_server, cmd,
	     tcvp_input_cdda_conf_cddb_user,
	     tcvp_input_cdda_conf_cddb_hostname,
	     tcvp_input_cdda_conf_cddb_client.name,
	     tcvp_input_cdda_conf_cddb_client.version);

    for(p = url; *p; p++)
	if(*p == ' ')
	    *p = '+';

    free(cmd);
/*     tc2_print("CDDA", TC2_PRINT_DEBUG, "%s\n", url); */

    u = url_http_open(url, "r");
    if(!u)
	return NULL;

    rsize = 1024;
    reply = malloc(rsize);
    p = reply;

    while((n = u->read(p, 1, rsize - (p - reply), u)) > 0){
	p += n;
	if(p - reply == rsize){
	    int s = p - reply;
	    reply = realloc(reply, rsize += 1024);
	    p = reply + s;
	}
    }

    *p = 0;
    u->close(u);

    return reply;
}

extern int
cdda_freedb(url_t *u, cd_data_t *cdt, int track)
{
    cdrom_drive *d = cdt->drive;
    int tracks = cdda_tracks(d);
    int sum = 0, secs;
    int sect, s, i;
    char *qry, *p;
    uint32_t id;
    char *rep, *cat = NULL;
    char *artist = NULL, *album = NULL, *title = NULL;

    qry = malloc(40 + tracks * 10);
    p = qry;

    while(tracks > 0 && !IS_AUDIO(d, tracks-1))
	tracks--;

    p += sprintf(qry, "cddb query XXXXXXXX %i", tracks);

    for(i = 0; i < tracks; i++){
	sect = d->disc_toc[i].dwStartSector + 150;
	p += sprintf(p, " %i", sect);
	s = sect / 75;
	while(s){
	    sum += s % 10;
	    s /= 10;
	}
    }

    s = d->disc_toc[tracks].dwStartSector / 75 -
	d->disc_toc[0].dwStartSector / 75;
    id = (sum & 0xff) << 24 | s << 8 | tracks;
    secs = (d->disc_toc[tracks].dwStartSector + 150) / 75;

    sprintf(p, " %i", secs);
    sprintf(qry + 11, "%08x", id);
    qry[19] = ' ';

    if((rep = cddb_cmd(qry))){
	char *rp;
	int status = strtol(rep, &rp, 0);

	switch(status){
	case 210:
	case 211:
	    if(!(rp = strchr(rp, '\n')))
		break;
	case 200:
	    cat = rp + 1;
	    break;
	}

	if(cat){
	    char *c = strchr(cat, ' ');

	    if(c)
		c = strchr(c + 1, ' ');
	    if(c)
		*c = 0;

	    snprintf(qry, 40, "cddb read %s", cat);
	    free(rep);
	    if((rep = cddb_cmd(qry))){
		char *l, *tmp = rep;
		char ttitle[12];
		int ttl;

		ttl = sprintf(ttitle, "TTITLE%i=", track - 1);

		while((l = strsep(&tmp, "\r\n"))){
		    if(!*l)
			continue;
		    if(*l == '#')
			continue;

		    if(!strncmp(l, "DTITLE=", 7)){
			char *t = strstr(l, " / ");
			if(t){
			    *t = 0;
			    album = strdup(t + 3);
			}
			artist = strdup(l + 7);
		    } else if(!strncmp(l, ttitle, ttl)){
			char *t = strstr(l, " / ");
			if(t)
			    *t = 0;
			title = strdup(l + ttl);
		    }
		}
	    }
	}
	free(rep);
    }

    if(artist)
	tcattr_set(u, "performer", artist, NULL, free);
    if(album)
	tcattr_set(u, "album", album, NULL, free);
    if(title)
	tcattr_set(u, "title", title, NULL, free);

    free(qry);
    return 0;
}

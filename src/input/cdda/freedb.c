/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
    char hname[128];
    char url[1024];
    char *reply;
    int rsize;
    url_t *u;
    char *p;
    int n;

    gethostname(hname, sizeof(hname));

    p = cmd = strdup(cmd);
    while(*p){
	*p = *p == ' '? '+': *p;
	p++;
    }

    snprintf(url, sizeof(url), "http://freedb.freedb.org/~cddb/cddb.cgi?"
	     "cmd=%s&hello=%s+%s+%s+%i.%i.%i&proto=1",
	     cmd, getenv("USER"), hname, MODULE_INFO.name,
	     MODULE_INFO.version >> 16,
	     (MODULE_INFO.version >> 8) & 0xff,
	     MODULE_INFO.version & 0xff);

    free(cmd);
/*     fprintf(stderr, "CDDA: %s\n", url); */

    u = url_http_open(url, "r");
    if(!u)
	return NULL;

    rsize = 1024;
    reply = malloc(rsize);
    p = reply;

    while((n = u->read(p, 1, 1024, u)) > 0){
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
cdda_freedb(url_t *u, int track)
{
    cd_data_t *cdt = u->private;
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
	case 201:
	    if(!(rp = strchr(rp, '\n')))
		break;
	case 200:
	    cat = rp + 1;
	    break;
	}

	if(cat){
	    char *c = strchr(cat, ' ');
	    if(c)
		*c = 0;

	    snprintf(qry, 40, "cddb read %s %08x", cat, id);
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

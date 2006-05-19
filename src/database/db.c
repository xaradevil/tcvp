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

#include <stdlib.h>
#include <stdio.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <pthread.h>
#include <sys/time.h>
#include <tcendian.h>
#include <tcvp_types.h>
#include <tchash.h>
#include <database_tc2.h>

typedef struct tcvp_database {
    eventq_t sc;
    pthread_mutex_t lock;
    tcconf_section_t *conf;
} tcvp_database_t;

struct tcdb {
    char *name;
    tchash_table_t *hash;
};

static int dbnum;
tchash_table_t *dbhash;

static void dbfree(void *p);
static int db_create(char *name);

extern tcdb_t *
get_dbref(char *name)
{
    void *p;

    if(!dbhash) {
	return NULL;
    }

    tchash_find(dbhash, name, -1, &p);

    return tcref(p);
}

extern char *
get_dbname(tcconf_section_t *cf)
{
    char *dbname;

    if(tcconf_getvalue(cf, "dbname", "%s", &dbname) < 1){
        dbname = malloc(16);
        snprintf(dbname, 16, "TCDB-%i", dbnum++);
        tcconf_setvalue(cf, "dbname", "%s", dbname);
    }

    return dbname;
}

static void
db_free(void *p)
{
    tcvp_database_t *tdb = p;

    tc2_print("database", TC2_PRINT_DEBUG+1, "db_free\n");

    eventq_delete(tdb->sc);

    pthread_mutex_destroy(&tdb->lock);

    if(dbhash) {
	tchash_destroy(dbhash, dbfree);
    }
    tcfree(tdb->conf);
}

extern int
db_init(tcvp_module_t *m)
{
    tcvp_database_t *tdb = m->private;
    char *dbname;

    tc2_print("database", TC2_PRINT_DEBUG+1, "db_init\n");

    tdb->sc = tcvp_event_get_sendq(tdb->conf, "control");
    dbhash = tchash_new(10, 1, 0);

    dbname = get_dbname(tdb->conf);
    db_create(dbname);
    free(dbname);
    
    return 0;
}

extern int
db_new(tcvp_module_t *m, tcconf_section_t *cs)
{
    tcvp_database_t *tdb;

    tc2_print("database", TC2_PRINT_DEBUG+1, "db_new\n");

    tdb = tcallocdz(sizeof(*tdb), NULL, db_free);
    pthread_mutex_init(&tdb->lock, NULL);
    tdb->conf = tcref(cs);
    m->private = tdb;

    return 0;
}


extern int
db_event_query(tcvp_module_t *p, tcvp_event_t *e)
{
    tcvp_database_t *tdb = p->private;

    tcvp_db_query_event_t *dbe = (tcvp_db_query_event_t*) e;

    tcdb_reply_t *re = db_query(get_dbref(dbe->dbname), dbe->query);

    if(re == NULL) return -1;

    tcvp_event_send(tdb->sc, TCVP_DB_REPLY, re->dbname, re->query,
		    re->reply, re->rtype);

    tcfree(re);

    return 0;
}


static void
db_reply_free(void *p)
{
    tcdb_reply_t *r = (tcdb_reply_t *) p;

    free(r->query);
    free(r->dbname);
    if(r->reply) free(r->reply);
}

static void
unescape(char *s)
{
    char *p = s;

    do {
	if(s[0] == '\\' && s[1] == '\'') {
	    s++;
	}
	*p++ = *s;
    } while(*s++);
}

extern tcdb_reply_t *
db_query(tcdb_t *db, char *query)
{
    tcdb_reply_t *r = NULL;

    tc2_print("database", TC2_PRINT_DEBUG+5, "dbname='%s' query='%s'\n",
	      db->name, query);

    if(strncmp(query, "ADD", 3) == 0) {
	char *q, *t, *k = NULL, *v = NULL;
	t = q = strdup(query);

	k = strchr(q, '\'');
	if(k) {
	    q=k;
	    do {
		q = strchr(q+1, '\'');
	    } while(q[-1]=='\\');
		
	    if(k && q) {
		*q=0;
		v = strchr(q+1, '\'');
		if(v) {
		    q=v+1;
		    do {
			q = strchr(q+1, '\'');
		    } while(q && q[-1]=='\\');
		    if(q) {
			*q=0;
		    }
		}
	    }
	}

	if(k && v) {
	    void *p = NULL;
	    k++;
	    v++;

	    unescape(k);
	    unescape(v);

	    tc2_print("database", TC2_PRINT_DEBUG+2, "ADD \"%s\" \"%s\"\n",
		      k, v);

	    tchash_replace(db->hash, k, -1, strdup(v), &p);
	    if(p) free(p);

	    r = tcallocdz(sizeof(*r), NULL, db_reply_free);

	    r->query = strdup(query);
	    r->dbname = strdup(db->name);
	    r->reply = strdup("");
	    r->rtype = TCDB_OK;
	}

	free(t);
    } else if(strncmp(query, "FIND", 4) == 0) {
	char *q, *t, *k = NULL;
	t = q = strdup(query);

	k = strchr(q, '\'');
	if(k) {
	    q=k;
	    do {
		q = strchr(q+1, '\'');
	    } while(q[-1]=='\\');
		
	    if(k && q) {
		*q=0;
	    }
	}

	if(k) {
	    void *p = NULL;
	    k++;

	    unescape(k);

	    tc2_print("database", TC2_PRINT_DEBUG+2, "FIND \"%s\"\n", k);

	    tchash_find(db->hash, k, -1, &p);

	    r = tcallocdz(sizeof(*r), NULL, db_reply_free);

	    r->query = strdup(query);
	    r->dbname = strdup(db->name);
	    r->reply = strdup(p?p:"");
	    r->rtype = p?TCDB_STRING:TCDB_FAIL;
	}

	free(t);	
    } else {
	tc2_print("database", TC2_PRINT_WARNING, "error in query '%s'\n",
		  query);
    }


    return r;
}


static void
dbfree(void *p)
{
    tcdb_t *tdb = p;

    tc2_print("database", TC2_PRINT_DEBUG+1, "dbfree\n");
    free(tdb->name);
    tchash_destroy(tdb->hash, free);
}


static int
db_create(char *name)
{
    void *p = NULL;
    tcdb_t *db = tcallocdz(sizeof(*db), NULL, dbfree);

    tc2_print("database", TC2_PRINT_DEBUG+1, "db_create\n");

    db->hash = tchash_new(10, 1, 0);
    db->name = strdup(name);

    tchash_replace(dbhash, name, -1, db, &p);

    if(p) tcfree(p);

    return 0;
}

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
#include <semaphore.h>
#include <sys/time.h>
#include <tcendian.h>
#include <tcvp_types.h>
#include <tchash.h>
#include <tcdbc_tc2.h>

typedef struct tcvp_dbc {
    char *dbname;
    eventq_t sc;
    pthread_mutex_t lock;
    tcconf_section_t *conf;
    tcdb_t *db;
    tchash_table_t *dbrhash;
} tcvp_dbc_t;

typedef struct db_reply {
    sem_t sem;
    tcdb_reply_t *reply;
} db_reply_t;


static void
dbc_free(void *p)
{
    tcvp_dbc_t *tdbc = p;

    tc2_print("dbc", TC2_PRINT_DEBUG+1, "dbc_free\n");

    tchash_destroy(tdbc->dbrhash, tcfree);

    eventq_delete(tdbc->sc);

    pthread_mutex_destroy(&tdbc->lock);

    if(tdbc->dbname) free(tdbc->dbname);
    if(tdbc->db) tcfree(tdbc->db);

    tcfree(tdbc->conf);
}


extern int
dbc_init(tcvp_module_t *m)
{
    tcvp_dbc_t *tdbc = m->private;
    char *qname, *qn;

    tc2_print("dbc", TC2_PRINT_DEBUG+1, "dbc_init\n");

    qname = tcvp_event_get_qname(tdbc->conf);
    qn = alloca(strlen(qname) + 9);

    tdbc->sc = eventq_new(NULL);

    sprintf(qn, "%s/control", qname);
    eventq_attach(tdbc->sc, qn, EVENTQ_SEND);

    free(qname);

    tdbc->dbrhash = tchash_new(10, 1, 0);

    return 0;
}


extern int
dbc_new(tcvp_module_t *m, tcconf_section_t *cs)
{
    tcvp_dbc_t *tdbc;

    tc2_print("dbc", TC2_PRINT_DEBUG+1, "dbc_new\n");

    tdbc = tcallocdz(sizeof(*tdbc), NULL, dbc_free);
    pthread_mutex_init(&tdbc->lock, NULL);
    tdbc->conf = tcref(cs);
    m->private = tdbc;

    return 0;
}


static void
db_reply_free(void *p)
{
    db_reply_t *dbr = p;

    tcfree(dbr->reply);
    sem_destroy(&dbr->sem);
}


extern tcdb_reply_t *
db_query(tcvp_module_t *m, char *q)
{
    tcvp_dbc_t *tdbc = m->private;    

    if(!tcconf_getvalue(tdbc->conf, "features/local/database", "")) {
	tc2_print("dbc", TC2_PRINT_DEBUG+8, "Local connection\n");
	if(tdbc->db == NULL) {
	    if(!tcconf_getvalue(tdbc->conf, "features/local/database", "")) {
		tdbc->dbname = tcvp_database_get_dbname(tdbc->conf);
		tdbc->db = tcvp_database_get_dbref(tdbc->dbname);
	    }
	    if(tdbc->db == NULL) {
		tc2_print("dbc", TC2_PRINT_ERROR,
			  "Database connection error\n");
		return NULL;
	    }
	}
	return tcvp_database_query(tdbc->db, q);
    } else if(!tcconf_getvalue(tdbc->conf, "features/database", "")){
	void *p=NULL;
	tc2_print("dbc", TC2_PRINT_DEBUG+8, "Remote connection\n");
	if(tdbc->dbname == NULL) {
	    tdbc->dbname = tcvp_database_get_dbname(tdbc->conf);
	    if(tdbc->dbname == NULL) {
		tc2_print("dbc", TC2_PRINT_ERROR,
			  "Database connection error\n");
		return NULL;
	    }
	}
	db_reply_t *dbr = tcallocdz(sizeof(*dbr), NULL, db_reply_free);
	sem_init(&dbr->sem, 0, 0);
	tchash_replace(tdbc->dbrhash, q, -1, dbr, &p);
	if(p) tcfree(p);

	tcvp_event_send(tdbc->sc, TCVP_DB_QUERY, tdbc->dbname, q);

	sem_wait(&dbr->sem);

	tcdb_reply_t *ret = tcref(dbr->reply);

	tcfree(dbr);

	return ret;
    } else {
	tc2_print("dbc", TC2_PRINT_ERROR, "No database available\n");
	return NULL;
    }
}


static void
tcvp_db_reply_free(void *p)
{
    tcdb_reply_t *tdbc = p;

    free(tdbc->query);
    free(tdbc->reply);
    free(tdbc->dbname);
}


extern int
db_reply(tcvp_module_t *m, tcvp_event_t *e)
{
    tcvp_dbc_t *tdbc = m->private;    
    tcvp_db_reply_event_t *dbr = (tcvp_db_reply_event_t *)e;

    tc2_print("dbc", TC2_PRINT_DEBUG+5, "Database reply '%s' '%s' '%i'\n",
	      dbr->dbname, dbr->reply, dbr->rtype);

    db_reply_t *reply = NULL;

    tchash_delete(tdbc->dbrhash, dbr->query, -1, &reply);
    if(reply == NULL) {
	return 0;
    }

    reply->reply = tcallocdz(sizeof(*reply->reply), NULL, tcvp_db_reply_free);

    reply->reply->query = strdup(dbr->query);
    reply->reply->dbname = strdup(dbr->dbname);
    reply->reply->reply = strdup(dbr->reply);
    reply->reply->rtype = dbr->rtype;

    sem_post(&reply->sem);

    return 0;
}

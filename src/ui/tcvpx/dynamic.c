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

#include "tcvpx.h"
#include <tcstring.h>
#include <ctype.h>

hash_table *variable_hash;
hash_table *widget_hash;

static char *
lookup_variable(char *key, void *p)
{
    void *var = NULL;

    hash_find(variable_hash, key, &var);

    return var;
}

extern int
change_variable(char *key, void *data)
{
    list_item *current = NULL;
    list *lst = NULL;
    char buf[1024];
    void *tmp = NULL;

    hash_delete(variable_hash, key, &tmp);
    if(tmp) free(tmp);

    hash_search(variable_hash, key, data, NULL);

    sprintf(buf, "var:%s", key);
    hash_find(widget_hash, buf, &lst);

    if(lst) {
	xtk_widget_t *w;

	while((w = list_next(lst, &current))!=NULL) {
	    widget_data_t *ad = w->data;
	    parse_variable(ad->value, &tmp, NULL);
	    switch(w->type) {
	    case TCSEEKBAR:
	    {
		double pos = (tmp)?*((double *)tmp):0;
		if(pos < 0) {
		    xtk_disable_seek_bar(w);
		} else {
		    xtk_enable_seek_bar(w);
		}
		xtk_change_seek_bar(w, pos);
		break;
	    }
	    }
	}
    }

    return 0;
}

extern int
change_text(char *key, char *text)
{
    list_item *current = NULL;
    list *lst = NULL;
    char buf[1024];
    void *tmp = NULL;

    hash_delete(variable_hash, key, &tmp);
    if(tmp) free(tmp);

    if(text) {
	hash_search(variable_hash, key, strdup(text), NULL);
    }

    sprintf(buf, "text:%s", key);
    hash_find(widget_hash, buf, &lst);

    if(lst) {
	xtk_widget_t *w;

	while((w = list_next(lst, &current))!=NULL) {
	    widget_data_t *ad = w->data;
	    parse_text(ad->value, buf);
	    switch(w->type) {
	    case TCLABEL:
		xtk_change_label(w, buf);
		break;
	    case TCSTATE:
		xtk_change_state(w, buf);
		break;
	    }
	}
    }

    return 0;
}


static int
get_keys(char *text, char ***keysp, char ***defaultsp)
{
    char **keys = NULL;
    char **defaults = NULL;
    char *foo, *src;
    int n = 0;

    foo = src = strdup(text);
    
    for(;;) {
	char *tmp;
	char *key;
	char *def=NULL;

	tmp = strstr(src, "${");
	if(!tmp) break;

	src = key = tmp+2;

	tmp = strchr(src, '}');
	tmp[0]=0;
	src = tmp+1;

	tmp = strchr(key, ':');
	if(tmp) {
	    tmp[0] = 0;
	    if(tmp[1]=='-') {
		def = tmp+2;
	    }
	}

	keys = realloc(keys, (n+1)*sizeof(*keys));
	keys[n] = strdup(key);
	if(defaultsp) {
	    defaults = realloc(defaults, (n+1)*sizeof(*defaults));
	    if(def) {
		defaults[n] = strdup(def);
	    } else {
		defaults[n] = NULL;
	    }
	}
	n++;
    }

    free(foo);

    *keysp = keys;
    if(defaultsp) {
	*defaultsp = defaults;
    }
    return n;
}


static int
register_key(char *key, xtk_widget_t *w)
{
    list *lst = NULL;
    hash_find(widget_hash, key, &lst);

    if(!lst) {
	lst = list_new(TC_LOCK_SLOPPY);
	hash_search(widget_hash, key, lst, NULL);
    }

    list_push(lst, w);

    return 0;
}

static int
unregister_key(char *key, xtk_widget_t *w)
{
    list *lst = NULL;
    hash_find(widget_hash, key, &lst);

    if(lst) {
	list_delete(lst, w, xtk_widget_cmp, NULL);
    }

    return 0;
}


extern int
register_textwidget(xtk_widget_t *w, char *text)
{
    char **keys;
    int n, i;

    n = get_keys(text, &keys, NULL);
    for(i=0; i<n; i++) {
	char buf[1024];
	sprintf(buf, "text:%s", keys[i]);
	register_key(buf, w);
	free(keys[i]);
    }

    free(keys);

    return 0;
}

extern int
unregister_textwidget(xtk_widget_t *w, char *text)
{
    char **keys;
    int n, i;

    n = get_keys(text, &keys, NULL);
    for(i=0; i<n; i++) {
	char buf[1024];
	sprintf(buf, "text:%s", keys[i]);
	unregister_key(buf, w);
	free(keys[i]);
    }

    free(keys);

    return 0;
}


extern int
register_varwidget(xtk_widget_t *w, char *text)
{
    char **keys;
    int n, i;

    n = get_keys(text, &keys, NULL);
    for(i=0; i<n; i++) {
	char buf[1024];
	sprintf(buf, "var:%s", keys[i]);
	register_key(buf, w);
	free(keys[i]);
    }

    free(keys);

    return 0;
}

extern int
unregister_varwidget(xtk_widget_t *w, char *text)
{
    char **keys;
    int n, i;

    n = get_keys(text, &keys, NULL);
    for(i=0; i<n; i++) {
	char buf[1024];
	sprintf(buf, "var:%s", keys[i]);
	unregister_key(buf, w);
	free(keys[i]);
    }

    free(keys);

    return 0;
}


extern int
parse_variable(char *text, void **result, void **def)
{
    char **keys, **defaults;
    int n, i;

    n = get_keys(text, &keys, &defaults);

    if(n>1) {
	for(i=0; i<n; i++) {
	    free(keys[i]);
	    free(defaults[i]);
	}
	free(keys);
	free(defaults);
    } else if(n == 1) {
	*result = lookup_variable(keys[0], NULL);
	if(!*result && def) {
	    if(defaults[0] && defaults[0][0] == '%' && defaults[0][1] == 'f') {
		double *dp = malloc(sizeof(*dp));
		*dp = strtod(defaults[0]+2, NULL);
		*def = dp;
	    }
	}
	free(keys[0]);
	free(defaults[0]);
	free(keys);
	free(defaults);
    }

    return 0;
}


extern int
parse_text(char *text, char *result)
{
    char *exp;

    if(!text) {
	result[0]=0;
	return 1;
    }

    exp = tcstrexp(text, "{", "}", ':', lookup_variable,
		   NULL, TCSTREXP_ESCAPE);
    strcpy(result, exp);
    free(exp);

    return 0;
}

extern int
init_dynamic(void)
{
    variable_hash = hash_new(10, 0);
    widget_hash = hash_new(10, 0);

    return 0;
}

static void
wh_free(void *p)
{
    list_destroy(p, NULL);
}

extern void
free_dynamic(void)
{
    hash_destroy(variable_hash, free);
    hash_destroy(widget_hash, wh_free);
}

/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include "tcvpx.h"
#include <tcstring.h>
#include <ctype.h>

tchash_table_t *variable_hash;
tchash_table_t *widget_hash;

static char *
lookup_variable(char *key, void *p)
{
    void *var = NULL;

    tchash_find(variable_hash, key, &var);

    return var;
}

extern int
change_variable(char *key, void *data)
{
    tclist_item_t *current = NULL;
    tclist_t *lst = NULL;
    char buf[1024];
    void *tmp = NULL;

    tchash_delete(variable_hash, key, &tmp);
    if(tmp) free(tmp);

    tchash_search(variable_hash, key, data, NULL);

    sprintf(buf, "var:%s", key);
    tchash_find(widget_hash, buf, &lst);

    if(lst) {
	xtk_widget_t *w;

	while((w = tclist_next(lst, &current))!=NULL) {
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
    tclist_item_t *current = NULL;
    tclist_t *lst = NULL;
    char buf[1024];
    void *tmp = NULL;

    tchash_delete(variable_hash, key, &tmp);
    if(tmp) free(tmp);

    if(text) {
	tchash_search(variable_hash, key, strdup(text), NULL);
    }

    sprintf(buf, "text:%s", key);
    tchash_find(widget_hash, buf, &lst);

    if(lst) {
	xtk_widget_t *w;

	while((w = tclist_next(lst, &current))!=NULL) {
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
    tclist_t *lst = NULL;
    tchash_find(widget_hash, key, &lst);

    if(!lst) {
	lst = tclist_new(TC_LOCK_SLOPPY);
	tchash_search(widget_hash, key, lst, NULL);
    }

    tclist_push(lst, w);

    return 0;
}

static int
unregister_key(char *key, xtk_widget_t *w)
{
    tclist_t *lst = NULL;
    tchash_find(widget_hash, key, &lst);

    if(lst) {
	tclist_delete(lst, w, xtk_widget_cmp, NULL);
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
    variable_hash = tchash_new(10, 0);
    widget_hash = tchash_new(10, 0);

    return 0;
}

static void
wh_free(void *p)
{
    tclist_destroy(p, NULL);
}

extern void
free_dynamic(void)
{
    tchash_destroy(variable_hash, free);
    tchash_destroy(widget_hash, wh_free);
}

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
#include <string.h>

hash_table *variable_hash;
hash_table *widget_hash;

static void*
lookup_variable(char *key)
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
	    action_data_t *ad = w->data;
	    parse_variable(ad->data, &tmp);
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
	    action_data_t *ad = w->data;
	    parse_text(ad->data, buf);
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
get_keys(char *text, char ***keysp)
{
    char **keys = NULL;
    char *foo, *src;
    int n = 0;

    foo = src = strdup(text);
    
    for(;;) {
	char *tmp;
	char *key;

	tmp = strstr(src, "${");
	if(!tmp) break;

	src = key = tmp+2;

	tmp = strchr(src, '}');
	tmp[0]=0;
	src = tmp+1;

	tmp = strchr(key, ':');
	if(tmp) {
	    tmp[0] = 0;
	}

	keys = realloc(keys, (n+1)*sizeof(*keys));
	keys[n] = strdup(key);
	n++;
    }

    free(foo);

    *keysp = keys;
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

    n = get_keys(text, &keys);
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

    n = get_keys(text, &keys);
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

    n = get_keys(text, &keys);
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

    n = get_keys(text, &keys);
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
parse_variable(char *text, void **result)
{
    char **keys;
    int n, i;

    n = get_keys(text, &keys);

    if(n>1) {
	for(i=0; i<n; i++) {
	    free(keys[i]);
	}
	free(keys);
    } else if(n == 1) {
	*result = lookup_variable(keys[0]);
	free(keys[0]);
	free(keys);
    }

    return 0;
}


extern int
parse_text(char *text, char *result)
{
    char *foo, *src, *dst;

    if(!text) {
	result[0]=0;
	return 1;
    }

    foo = src = strdup(text);
    dst = result;

    for(;;) {
	char *tmp;
	char *key;
	char *default_text = NULL;

	tmp = strstr(src, "${");
	if(!tmp) break;

	memcpy(dst, src, tmp-src);
	dst += tmp-src;

	src = key = tmp+2;

	tmp = strchr(src, '}');
	tmp[0]=0;
	src = tmp+1;

	tmp = strchr(key, ':');
	if(tmp) {
	    tmp[0] = 0;
	    tmp += 2;
	    default_text = tmp;
	}

	tmp = lookup_variable(key);

	if(tmp) {
	    memcpy(dst, tmp, strlen(tmp));
	    dst += strlen(tmp);	    
	} else if(default_text) {
	    memcpy(dst, default_text, strlen(default_text));
	    dst += strlen(default_text);
	}
    }

    memcpy(dst, src, strlen(src));
    dst[strlen(src)] = 0;

    free(foo);

    return 0;
}

extern int
init_dynamic()
{
    variable_hash = hash_new(10, 0);
    widget_hash = hash_new(10, 0);

    return 0;
}

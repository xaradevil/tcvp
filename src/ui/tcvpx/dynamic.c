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
#include "widgets.h"

hash_table *text_hash;
hash_table *widget_hash;

static char *
lookup_text(char *key)
{
    char *text = NULL;

    hash_find(text_hash, key, &text);

    return text;
}

extern int
change_text(char *key, char *text)
{
    list_item *current = NULL;
    list *lst = NULL;
    char buf[1024];
    void *tmp = NULL;

    hash_delete(text_hash, key, &tmp);
    if(tmp) free(tmp);

    hash_search(text_hash, key, strdup(text), NULL);

    sprintf(buf, "text:%s", key);
    hash_find(widget_hash, buf, &lst);

    if(lst) {
	tcwidget_t *w;

	while((w = list_next(lst, &current))!=NULL) {
	    action_data_t *ad = ((tclabel_t*)w)->data;
	    parse_text(ad->data, buf);
	    switch(w->common.type) {
	    case TCLABEL:
		change_label((tclabel_t*)w, buf);
		break;
	    case TCSTATE:
		change_state((tcstate_t*)w, buf);
		break;
	    }
	}
    }

    return 0;
}

static int
register_key(char *key, tcwidget_t *w)
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

extern int
register_textwidget(tcwidget_t *w, char *text)
{
    char *foo, *src;

    foo = src = strdup(text);
    
    for(;;) {
	char *tmp;
	char *key;
	char *default_text = NULL;
	char buf[1024];

	tmp = strchr(src, '%');
	if(!tmp) break;

	src = key = tmp+1;

	tmp = strchr(key, '=');
	if(tmp) {
	    tmp[0] = 0;
	    tmp++;
	    default_text = strchr(tmp, '\'') + 1;
	    tmp = strchr(default_text, '\'');
	    tmp[0] = 0;
	    src = tmp+1;
	}

	tmp = strchr(src, '%');
	tmp[0]=0;
	src = tmp+1;

	sprintf(buf, "text:%s", key);
	register_key(buf, w);
    }

    free(foo);

    return 0;
}

static int
unregister_key(char *key, tcwidget_t *w)
{
    list *lst = NULL;
    hash_find(widget_hash, key, &lst);

    if(lst) {
	list_delete(lst, w, widget_cmp, NULL);
    }

    return 0;
}

extern int
unregister_textwidget(tcwidget_t *w, char *text)
{
    char *foo, *src;

    foo = src = strdup(text);
    
    for(;;) {
	char *tmp;
	char *key;
	char *default_text = NULL;
	char buf[1024];

	tmp = strchr(src, '%');
	if(!tmp) break;

	src = key = tmp+1;

	tmp = strchr(key, '=');
	if(tmp) {
	    tmp[0] = 0;
	    tmp++;
	    default_text = strchr(tmp, '\'') + 1;
	    tmp = strchr(default_text, '\'');
	    tmp[0] = 0;
	    src = tmp+1;
	}

	tmp = strchr(src, '%');
	tmp[0]=0;
	src = tmp+1;

	sprintf(buf, "text:%s", key);
	unregister_key(buf, w);
    }

    free(foo);

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

	tmp = strchr(src, '%');
	if(!tmp) break;

	memcpy(dst, src, tmp-src);
	dst += tmp-src;

	src = key = tmp+1;

	tmp = strchr(key, '=');
	if(tmp) {
	    tmp[0] = 0;
	    tmp++;
	    default_text = strchr(tmp, '\'') + 1;
	    tmp = strchr(default_text, '\'');
	    tmp[0] = 0;
	    src = tmp+1;
	}

	tmp = strchr(src, '%');
	tmp[0]=0;
	src = tmp+1;

	tmp = lookup_text(key);

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
    text_hash = hash_new(10, 0);
    widget_hash = hash_new(10, 0);

    return 0;
}

/**
    Copyright (C) 2003, 2004  Michael Ahlberg, Måns Rullgård

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

#include "tcvpx.h"
#include <tcstring.h>
#include <ctype.h>
#include <iconv.h>

tchash_table_t *variable_hash;
tchash_table_t *widget_hash;

static char *
lookup_variable(char *key, void *p)
{
    void *var = NULL;

    tchash_find(variable_hash, key, -1, &var);

    return var;
}

extern int
change_variable(char *key, void *data)
{
    tclist_item_t *current = NULL;
    tclist_t *lst = NULL;
    char buf[1024];
    void *tmp = NULL;

    tchash_delete(variable_hash, key, -1, &tmp);
    if(tmp) free(tmp);

    tchash_search(variable_hash, key, -1, data, NULL);

    sprintf(buf, "var:%s", key);
    tchash_find(widget_hash, buf, -1, &lst);

    if(lst) {
	xtk_widget_t *w;

	while((w = tclist_next(lst, &current))!=NULL) {
	    XTK_SLIDER(w, s);
	    widget_data_t *ad = w->data;
	    parse_variable(ad->value, &tmp, NULL);
	    if(s) {
		double pos = (tmp)?*((double *)tmp):0;
		if(pos < 0) {
		    xtk_widget_disable(w);
		} else {
		    xtk_widget_enable(w);
		}
		xtk_widget_slider_set_position(w, pos);
		break;
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

    tchash_delete(variable_hash, key, -1, &tmp);
    if(tmp) free(tmp);

    if(text) {
	tchash_search(variable_hash, key, -1, strdup(text), NULL);
    }

    sprintf(buf, "text:%s", key);
    tchash_find(widget_hash, buf, -1, &lst);

    tc2_print("tcvpx", TC2_PRINT_DEBUG + 10,
	      "change_text '%s' '%s' '%s' %p\n", key, buf, text, lst);

    if(lst) {
	xtk_widget_t *w;
	while((w = tclist_next(lst, &current))!=NULL) {
	    widget_data_t *ad = w->data;
	    parse_text(ad->value, buf, 1024);
	    xtk_widget_label_set_text(w, buf);
	    xtk_widget_state_set_state(w, buf);
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

    tc2_print("tcvpx", TC2_PRINT_DEBUG + 10, "register_key %s\n", key);

    tchash_find(widget_hash, key, -1, &lst);

    if(!lst) {
	tc2_print("tcvpx", TC2_PRINT_DEBUG + 10, "key: %s not yet registered\n", key);
	lst = tclist_new(TC_LOCK_SLOPPY);
	tchash_search(widget_hash, key, -1, lst, NULL);
    }

    tclist_push(lst, w);

    return 0;
}

int
ptr_cmp(const void *p1, const void *p2)
{
    return p1-p2;
}

static int
unregister_key(char *key, xtk_widget_t *w)
{
    tclist_t *lst = NULL;
    tchash_find(widget_hash, key, -1, &lst);

    if(lst) {
	tclist_delete(lst, w, ptr_cmp, NULL);
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
parse_text(char *text, char *result, int len)
{
    char *exp;
    char *outptr = result, *inptr;
    iconv_t ic;
    int avail = len, insize;

    if(!text) {
	result[0]=0;
	return 1;
    }

    tc2_print("tcvpx", TC2_PRINT_DEBUG + 10, "parse_text: text=%s\n", text);

    exp = tcstrexp(text, "{", "}", ':', lookup_variable,
		   NULL, TCSTREXP_ESCAPE);
    inptr = exp;
    insize = strlen(exp);

    ic = iconv_open("UTF-8", "ISO_8859-1");
    iconv(ic, NULL, NULL, &outptr, &avail);
    iconv(ic, &inptr, &insize, &outptr, &avail);
    if(avail >= 1) *outptr = '\0';
    iconv_close(ic);

    free(exp);

    tc2_print("tcvpx", TC2_PRINT_DEBUG + 10, "parse_text: result=%s\n", result);

    return 0;
}

extern int
init_dynamic(void)
{
    variable_hash = tchash_new(10, 0, 0);
    widget_hash = tchash_new(10, 0, 0);

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
    if(variable_hash)
	tchash_destroy(variable_hash, free);
    if(widget_hash)
	tchash_destroy(widget_hash, wh_free);
}

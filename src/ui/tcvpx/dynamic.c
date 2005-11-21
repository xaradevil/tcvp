/**
    Copyright (C) 2003 - 2005  Michael Ahlberg, Måns Rullgård

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
tcvp_module_t *dbc = NULL;

static char *
lookup_variable(char *key, void *p)
{
    void *var = NULL;

    tchash_find(variable_hash, key, -1, &var);

    return var;
}

static char *
escape_string(char *src)
{
    char *ret = malloc(2*strlen(src)+1);
    char *dst = ret;
    do {
	if (*src == '\'') *dst++ = '\\';
	*dst++ = *src;
    } while(*src++);

    return ret;
}

static char *
lookup_db_attr(char *n, void *p)
{
    char *f = (char*) p;
    char *k, *fe, *ne;
    tcdb_reply_t *r;

    if(strcmp("file", n) == 0) {
	return strdup(f);
    }

    fe = escape_string(f);
    ne = escape_string(n);

    k = calloc(strlen(fe) + strlen(ne) + 20, 1);

    sprintf(k, "FIND '%s/%s'", fe, ne);

    r = tcvp_tcdbc_query(dbc, k);

    free(k);

//   fprintf(stderr, "%s\n", (r && r->rtype == TCDB_STRING)?r->reply:"ERROR");

    k = (r && r->rtype == TCDB_STRING && r->reply)?strdup(r->reply):NULL;
    if(r) tcfree(r);

    free(fe);
    free(ne);

    return k;
}


extern int
change_variable(char *key, char *datatype, void *data)
{
    tclist_item_t *current1 = NULL, *current2 = NULL;
    tclist_t *wlst = NULL, *cblst = NULL;
    char buf[1024];
    void *tmp = NULL;

    tchash_delete(variable_hash, key, -1, &tmp);
    if(tmp) tcfree(tmp);

    tchash_search(variable_hash, key, -1, data, NULL);

    sprintf(buf, "varwidget:%s:%s", datatype, key);
    tchash_find(widget_hash, buf, -1, &wlst);

    sprintf(buf, "varcb:%s:%s", datatype, key);
    tchash_find(widget_hash, buf, -1, &cblst);

    tc2_print("tcvpx", TC2_PRINT_DEBUG + 10,
	      "change_variable '%s' '%s' %p\n", key, buf, wlst);

    if(wlst && cblst) {
	xtk_widget_t *w;
	action_cb_t cb;

	while((w = tclist_next(wlst, &current1))!=NULL &&
	      (cb = tclist_next(cblst, &current2))!=NULL) {
	    if(data == NULL) {
		widget_data_t *ad = xtk_widget_get_data(w);
		if(ad) {
		    parse_variable(datatype, ad->value, &tmp, NULL);
		    if(tmp) cb(w, tmp);
		}
	    } else if(strcmp(datatype, "string_array") == 0) {
		widget_data_t *ad = xtk_widget_get_data(w);
		if(ad) {
		    if(ad->value) {
			parse_variable(datatype, ad->value, &tmp, NULL);
			if(tmp) {
			    cb(w, tmp);
			    tcfree(tmp);
			}
		    } else if(ad->nvalues > 0) {
			int i;
			for(i = 0; i<ad->nvalues; i++) {
			    if(strncmp(ad->values[i], key, strlen(key))== 0) {
				parse_variable(datatype, ad->values[i], &tmp,
					       NULL);
				if(tmp) {
				    cb(w, tmp);
				    tcfree(tmp);
				}
			    }
			}
		    }
		} else {
		    cb(w, data);
		}		
	    } else {
		cb(w, data);
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
    if(tmp) tcfree(tmp);

    if(text) {
	char *txttmp = tcalloc(strlen(text)+1);
	memcpy(txttmp, text, strlen(text)+1);
	tchash_search(variable_hash, key, -1, txttmp, NULL);
    }

    sprintf(buf, "text:%s", key);
    tchash_find(widget_hash, buf, -1, &lst);

    tc2_print("tcvpx", TC2_PRINT_DEBUG + 10,
	      "change_text '%s' '%s' '%s' %p\n", key, buf, text, lst);

    if(lst) {
	xtk_widget_t *w;
	while((w = tclist_next(lst, &current))!=NULL) {
	    widget_data_t *ad = xtk_widget_get_data(w);
	    if(ad) {
		parse_text(ad->value, buf, 1024);
		xtk_widget_label_set_text(w, buf);
		xtk_widget_state_set_state(w, buf);
		XTK_WINDOW(w, win);
		if(win){
		    xtk_window_set_title(w, buf);
		}
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
register_key(char *key, void *w)
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
unregister_key(char *key, void *w)
{
    tclist_t *lst = NULL;

    tc2_print("tcvpx", TC2_PRINT_DEBUG + 10, "unregister_key %s\n", key);

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

    tcref(w);

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

    tcfree(w);

    return 0;
}


extern int
register_varwidget(xtk_widget_t *w, action_cb_t cb, char *datatype,
		   char *value)
{
    tcref(w);
    char *tmp, *valueparsed;
    char buf[1024];

    valueparsed = strdup(value);

    tmp = strchr(valueparsed, '|');
    if(tmp) {
	*tmp=0;
    }

    sprintf(buf, "varwidget:%s:%s", datatype, valueparsed);
    register_key(buf, w);
    sprintf(buf, "varcb:%s:%s", datatype, valueparsed);
    register_key(buf, cb);

    free(valueparsed);

    return 0;
}

extern int
unregister_varwidget(xtk_widget_t *w, action_cb_t cb, char *datatype,
		     char *value)
{
    char *valueparsed, *tmp;
    char buf[1024];

    valueparsed = strdup(value);

    tmp = strchr(valueparsed, '|');
    if(tmp) {
	*tmp=0;
    }

    sprintf(buf, "varwidget:%s:%s", datatype, valueparsed);
    unregister_key(buf, w);
    sprintf(buf, "varcb:%s:%s", datatype, valueparsed);
    unregister_key(buf, cb);

    tcfree(w);

    free(valueparsed);

    return 0;
}


extern int
parse_variable(char *datatype, char *text, void **result, void **def)
{
    char *key, *tmp, *attr = NULL, *dflt = NULL;

    key = strdup(text);

    tmp = strchr(key, '|');
    if(tmp) {
	*tmp = 0;
	attr = tmp+1;
    }    

    tmp = strchr(key, ':');
    if(tmp && attr == NULL) {
	tmp[0] = 0;
	if(tmp[1]=='-') {
	    dflt = tmp+2;
	}
    }

    *result = lookup_variable(key, NULL);

    if(*result == NULL && dflt != NULL && def != NULL) {
	if(dflt[0] == '%' && dflt[1] == 'f') {
	    double *dp = tcalloc(sizeof(*dp));
	    *dp = strtod(dflt+2, NULL);
	    *def = dp;
	} else if (dflt[0] == '%' && dflt[1] == 'd') {
	    double *dp = tcalloc(sizeof(*dp));
	    *dp = strtol(dflt+2, NULL, 0);
	    *def = dp;
	}
    }

    if(*result != NULL && attr != NULL &&
       strcmp(datatype, "string_array") == 0) {
	int n, i;
	char **list = (char **)*result;
	for(n=0; list[n] != NULL; n++);

	char **entries_formatted = 
	    tcallocd((n+1) * sizeof(*entries_formatted), NULL, plarrayfree);
	for(i=0; i<n; i++) {
	    entries_formatted[i] = tcstrexp(attr, "{", "}", ':',
					    lookup_db_attr, list[i],
					    TCSTREXP_FREE | TCSTREXP_ESCAPE);
	}
	entries_formatted[i] = NULL;

	*result = entries_formatted;
    } else if(strcmp(datatype, "string_array") == 0) {
	if(*result) tcref(*result);
    }

    free(key);

    return 0;
}


extern int
parse_text(char *text, char *result, int len)
{
    char *exp;

    if(!text) {
	result[0]=0;
	return 1;
    }

    tc2_print("tcvpx", TC2_PRINT_DEBUG + 10, "parse_text: text=%s\n", text);

    exp = tcstrexp(text, "{", "}", ':', lookup_variable,
		   NULL, TCSTREXP_ESCAPE);
    strncpy(result, exp, len);
    result[len-1] = 0;
    free(exp);

    tc2_print("tcvpx", TC2_PRINT_DEBUG + 10, "parse_text: result=%s\n", result);

    return 0;
}

extern int
init_dynamic(tcconf_section_t *cf)
{
    variable_hash = tchash_new(10, 0, 0);
    widget_hash = tchash_new(10, 0, 0);

    dbc = tcvp_tcdbc_new(cf);
    dbc->init(dbc);

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
	tchash_destroy(variable_hash, tcfree);
    if(widget_hash)
	tchash_destroy(widget_hash, wh_free);
    tcfree(dbc);
}

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
#include <ctype.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcendian.h>
#include <tchash.h>
#include <tcarg.h>
#include <keys_tc2.h>

typedef struct value {
    int type;
    union {
	int64_t i;
	double f;
	char *s;
    } value;
} value_t;

#define VALUE_TYPE_INT    1
#define VALUE_TYPE_FLOAT  2
#define VALUE_TYPE_STRING 3
#define VALUE_TYPE_VAR    4

typedef struct action action_t;
typedef int (*function_t)(tcvp_module_t *, char *key, char *origin,
			  action_t *);

struct action {
    char *name;
    function_t func;
    int nargs;
    value_t **args;
};

typedef struct key_handler {
    char *name;
    function_t func;
} key_handler_t;

typedef struct key_binding {
    char *key;
    char *origin;
    action_t *action;
} key_binding_t;

typedef struct tcvp_keys {
    eventq_t control;
    tcconf_section_t *conf;
} tcvp_keys_t;

static tchash_table_t *bindings;

static int
ac_send(tcvp_module_t *tm, char *key, char *origin, action_t *ac)
{
    tcvp_keys_t *tk = tm->private;
    value_t *arg = ac->args[0];
    tcarg_t evt_args;
    int evt;
    int i;

    if(ac->nargs < 1)
	return -1;

    if(arg->type != VALUE_TYPE_STRING && arg->type != VALUE_TYPE_VAR)
	return -1;

    evt = tcvp_event_get(arg->value.s);
    tcarg_init(evt_args);

    for(i = 1; i < ac->nargs; i++){
	arg = ac->args[i];
	switch(arg->type){
	case VALUE_TYPE_INT:
	    tcarg_add(evt_args, int64_t, arg->value.i);
	    break;
	case VALUE_TYPE_FLOAT:
	    tcarg_add_float(evt_args, double, arg->value.f);
	    break;
	case VALUE_TYPE_STRING:
	case VALUE_TYPE_VAR:
	    tcarg_add(evt_args, char *, arg->value.s);
	    break;
	}
    }

    tcvp_event_sendv(tk->control, evt, tcarg_va_list(evt_args));
    tcarg_free(evt_args);

    return 0;
}

static int
ac_quit(tcvp_module_t *tm, char *key, char *origin, action_t *ac)
{
    tc2_request(TC2_UNLOAD_ALL, 0);
    return 0;
}

static key_handler_t handlers[] = {
    { "send", ac_send },
    { "quit", ac_quit },
    { }
};

static function_t
find_func(char *name)
{
    int i;

    for(i = 0; handlers[i].name; i++)
	if(!strcmp(name, handlers[i].name))
	    return handlers[i].func;

    return NULL;
}

static void
free_action(void *p)
{
    action_t *ac = p;
    int i;

    for(i = 0; i < ac->nargs; i++){
	value_t *v = ac->args[i];
	if(v->type == VALUE_TYPE_STRING || v->type == VALUE_TYPE_VAR)
	    free(v->value.s);
    }

    free(ac->name);
    free(ac->args);
    free(ac);
}

static char *
skipws(char *p)
{
    while(isspace(*p))
	p++;

    return p;
}

static int
wordlen(char *p)
{
    char *q = p;

    while(isalnum(*q) || *q == '_')
	q++;

    return q - p;
}

static char *
mkstr(char *p, int n)
{
    char *c = malloc(n + 1);
    strncpy(c, p, n);
    c[n] = 0;
    return c;
}

static int
add_arg(action_t *ac, value_t *val)
{
    ac->args = realloc(ac->args, ++ac->nargs * sizeof(*ac->args));
    ac->args[ac->nargs - 1] = val;
    return 0;
}

static action_t *
parse_action(char *s)
{
    action_t *ac = calloc(1, sizeof(*ac));
    value_t *val;
    int n;

    s = skipws(s);
    n = wordlen(s);
    if(n <= 0){
	tc2_print("KEYS", TC2_PRINT_WARNING, "expecting word, found %c\n", *s);
	goto err;
    }

    ac->name = mkstr(s, n);
    s += n;
    ac->func = find_func(ac->name);
    if(!ac->func){
	tc2_print("KEYS", TC2_PRINT_WARNING, "function '%s' not found\n",
		  ac->name);
	goto err;
    }

    s = skipws(s);
    if(!*s){
	return ac;
    } else if(*s == '('){
	s++;
    } else {
	tc2_print("KEYS", TC2_PRINT_WARNING, "expecting '(', found %c\n", *s);
	goto err;
    }

    while(*s){
	s = skipws(s);

	if(isdigit(*s) || *s == '-'){
	    char *e;
	    int64_t v;
	    double d;

	    val = calloc(1, sizeof(*val));

	    v = strtoll(s, &e, 0);
	    if(*e == '.'){
		d = strtod(s, &e);
		val->type = VALUE_TYPE_FLOAT;
		val->value.f = d;
	    } else {
		val->type = VALUE_TYPE_INT;
		val->value.i = v;
	    }

	    add_arg(ac, val);
	    s = e;
	} else if(isalpha(*s) || *s == '_'){
	    n = wordlen(s);
	    val = calloc(1, sizeof(*val));
	    val->type = VALUE_TYPE_VAR;
	    val->value.s = mkstr(s, n);
	    s += n;
	    add_arg(ac, val);
	} else if(*s == '"' || *s == '\''){
	    char *e = s + 1;
	    while(*e && *e != *s)
		e++;
	    if(*e != *s)
		goto err;
	    val = calloc(1, sizeof(*val));
	    val->type = VALUE_TYPE_STRING;
	    val->value.s = mkstr(s, e - s);
	    add_arg(ac, val);
	    s = e + 1;
	} else {
	    goto err;
	}

	s = skipws(s);
	if(*s == ',')
	    s++;
	else if(*s == ')')
	    break;
	else
	    goto err;
    }

    return ac;

  err:
    free_action(ac);
    return NULL;
}

static action_t *
find_binding(tcvp_module_t *m, char *key)
{
    action_t *ac = NULL;
    tchash_find(bindings, key, -1, &ac);
    return ac;
}

extern int
key_event(tcvp_module_t *m, tcvp_event_t *te)
{
    tcvp_key_event_t *ke = (tcvp_key_event_t *) te;
    action_t *ac = find_binding(m, ke->key);

    if(!ac)
	return 0;

    ac->func(m, ke->key, NULL, ac);
    return 0;
}

static void
keys_free(void *p)
{
    tcvp_keys_t *tk = p;
    eventq_delete(tk->control);
    tcfree(tk->conf);
}

extern int
keys_init(tcvp_module_t *m)
{
    tcvp_keys_t *tk = m->private;
    char *qname, *qn;

    if(!tcconf_getvalue(tk->conf, "features/keys", ""))
	return -1;

    qname = tcvp_event_get_qname(tk->conf);
    qn = alloca(strlen(qname) + 9);

    tk->control = eventq_new(NULL);

    sprintf(qn, "%s/control", qname);
    eventq_attach(tk->control, qn, EVENTQ_SEND);

    tcconf_setvalue(tk->conf, "features/keys", "");
    tcconf_setvalue(tk->conf, "features/local/keys", "");

    free(qname);
    return 0;
}

extern int
keys_new(tcvp_module_t *m, tcconf_section_t *cs)
{
    tcvp_keys_t *tk;

    tk = tcallocdz(sizeof(*tk), NULL, keys_free);
    tk->conf = tcref(cs);
    m->private = tk;

    return 0;
}

extern int
keys_init_bindings(char *p)
{
    int i;

    bindings = tchash_new(10, TC_LOCK_NONE, 0);

    for(i = 0; i < tcvp_keys_conf_bind_count; i++){
	action_t *ac = parse_action(tcvp_keys_conf_bind[i].action);
	action_t *oac = NULL;

	if(!ac){
	    tc2_print("KEYS", TC2_PRINT_WARNING,
		      "syntax error in key action: %s\n",
		      tcvp_keys_conf_bind[i].action);
	    continue;
	}

	tchash_replace(bindings, tcvp_keys_conf_bind[i].key, -1, ac, &oac);
	if(oac)
	    free_action(oac);
    }

    return 0;
}

extern int
keys_free_bindings(void)
{
    tchash_destroy(bindings, free_action);
    return 0;
}

static void
key_free(void *p)
{
    tcvp_key_event_t *te = p;
    free(te->key);
}

extern void *
key_alloc(int type, va_list args)
{
    tcvp_key_event_t *te = tcvp_event_alloc(type, sizeof(*te), key_free);
    te->key = strdup(va_arg(args, char *));
    return te;
}

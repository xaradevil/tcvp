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
#include <tcscript.h>
#include <keys_tc2.h>

typedef struct key_handler {
    char *name;
    tcscript_function_t func;
} key_handler_t;

typedef struct key_binding {
    char *key;
    char *origin;
    tcscript_t *action;
} key_binding_t;

typedef struct tcvp_keys {
    eventq_t control;
    tcconf_section_t *conf;
    tchash_table_t *bindings;
    tcscript_namespace_t *ns;
} tcvp_keys_t;

static tcscript_value_t *
ac_send(tcscript_t *tcs, void *p, char *fun, int na, tcscript_value_t **args)
{
    tcvp_module_t *tm = p;
    tcvp_keys_t *tk = tm->private;
    tcarg_t evt_args;
    char *fmt, **str;
    int evt;
    int i;

    if(na < 1)
	return NULL;

    evt = tcscript_getint(args[0]);
    fmt = tcvp_event_format(evt);

    if(!fmt){
	tc2_print("KEYS", TC2_PRINT_WARNING,
		  "format of event %i unknown\n", evt);
	return NULL;
    }

    na--;
    args++;

    if(strlen(fmt) != na){
	tc2_print("KEYS", TC2_PRINT_WARNING,
		  "incorrect number of arguments for event %i: %i\n", evt, na);
	return 0;
    }

    tcarg_init(evt_args);
    str = calloc(na, sizeof(*str));

    for(i = 0; i < na; i++){
	switch(fmt[i]){
	case 'i':
	case 'u':
	    tcarg_add(evt_args, int32_t, tcscript_getint(args[i]));
	    break;
	case 'I':
	case 'U':
	    tcarg_add(evt_args, int64_t, tcscript_getint(args[i]));
	    break;
	case 'f':
	    tcarg_add_float(evt_args, double, tcscript_getfloat(args[i]));
	    break;
	case 's':
	    str[i] = tcscript_getstring(args[i]);
	    tcarg_add(evt_args, char *, str[i]);
	    break;
	case 'p':
	    tcarg_add(evt_args, void *, tcscript_getptr(args[i]));
	    break;
	}
    }

    tcvp_event_sendv(tk->control, evt, tcarg_va_list(evt_args));
    tcarg_free(evt_args);

    for(i = 0; i < na; i++)
	free(str[i]);
    free(str);

    return NULL;
}

static tcscript_value_t *
ac_quit(tcscript_t *tcs, void *p, char *fun, int na, tcscript_value_t **args)
{
    tc2_request(TC2_UNLOAD_ALL, 0);
    return NULL;
}

static key_handler_t handlers[] = {
    { "send", ac_send },
    { "quit", ac_quit },
    { }
};

static tcscript_function_t
find_func(char *name)
{
    int i;

    for(i = 0; handlers[i].name; i++)
	if(!strcmp(name, handlers[i].name))
	    return handlers[i].func;

    return NULL;
}

static tcscript_value_t *
find_sym(void *p, char *name)
{
    tcscript_function_t acf = find_func(name);

    if(acf)
	return tcscript_const(tcscript_mkfun(acf));

    if(!strncmp(name, "TCVP", 4))
	return tcscript_const(tcscript_mkint(tcvp_event_get(name)));

    return NULL;
}

static tcscript_t *
find_binding(tcvp_module_t *m, char *key)
{
    tcvp_keys_t *tk = m->private;
    tcscript_t *ac = NULL;
    tchash_find(tk->bindings, key, -1, &ac);
    return ac;
}

extern int
key_event(tcvp_module_t *m, tcvp_event_t *te)
{
    tcvp_key_event_t *ke = (tcvp_key_event_t *) te;
    tcscript_t *ac = find_binding(m, ke->key);

    if(!ac)
	return 0;

    tcscript_run(ac);

    return 0;
}

static int
keys_init_bindings(tcvp_module_t *m)
{
    tcvp_keys_t *tk = m->private;
    int i;

    tk->bindings = tchash_new(10, TC_LOCK_NONE, 0);
    tk->ns = tcscript_ns_alloc(find_sym, NULL, m);

    for(i = 0; i < tcvp_keys_conf_bind_count; i++){
	char *action = tcvp_keys_conf_bind[i].action;
	char *key = tcvp_keys_conf_bind[i].key;
	tcscript_t *ac = tcscript_compile_string(action, tk->ns);
	tcscript_t *oac = NULL;

	if(!ac){
	    tc2_print("KEYS", TC2_PRINT_WARNING,
		      "error in key action: %s\n",
		      tcvp_keys_conf_bind[i].action);
	    continue;
	}

	tchash_replace(tk->bindings, key, -1, ac, &oac);
	tcfree(oac);
    }

    return 0;
}

static void
keys_free(void *p)
{
    tcvp_keys_t *tk = p;
    eventq_delete(tk->control);
    tcfree(tk->conf);
    if(tk->bindings)
	tchash_destroy(tk->bindings, tcfree);
    tcfree(tk->ns);
}

extern int
keys_init(tcvp_module_t *m)
{
    tcvp_keys_t *tk = m->private;
    char *qname, *qn;

    if(!tcconf_getvalue(tk->conf, "features/keys", ""))
	return -1;

    keys_init_bindings(m);

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

/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

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

#ifndef _TCVPX_H
#define _TCVPX_H

#include "tcvpx_tc2.h"
#include <tchash.h>

typedef struct tcvpx {
    tcconf_section_t *conf;
} tcvpx_t;

#define STOPPED 0
#define PLAYING 1
#define PAUSED 2

#define TCTIME_ELAPSED   0
#define TCTIME_REMAINING 1

#define ST_STICKY (1<<0)
#define ST_ON_TOP (1<<1)

typedef struct {
    tcconf_section_t *config;
    char *file;
    char *path;
    int width, height;
    xtk_widget_t *window;
    int state;
    tchash_table_t *id_hash;
    tclist_t *templates;
    char *dblclick;
} skin_t;

extern skin_t* load_skin(char *skinfile);
extern int create_ui(xtk_widget_t *c, skin_t *skin,
		     tcconf_section_t *config, tchash_table_t *parameters);

extern int lookup_action(xtk_widget_t *w, void *p);
extern int init_skins(void);
extern void cleanup_skins(void);

extern int init_dynamic(void);
extern void free_dynamic(void);

extern void free_ctl(void);

extern int parse_text(char *text, char *result, int len);
extern int parse_variable(char *text, void **result, void **def);

extern int change_text(char *key, char *text);
extern int change_variable(char *key, void *data);

extern int unregister_textwidget(xtk_widget_t *w, char *text);
extern int register_textwidget(xtk_widget_t *w, char *text);

extern int unregister_varwidget(xtk_widget_t *w, char *text);
extern int register_varwidget(xtk_widget_t *w, char *text);

extern int update_time(void);
extern int update_state(char *state);

extern void *tcvp_event(void *p);
extern int toggle_time(xtk_widget_t *w, void *p);

extern int sticky_cb(xtk_widget_t *xw, int i);
extern int on_top_cb(xtk_widget_t *xw, int i);


extern int quit;

extern eventq_t qs;
extern eventq_t qr;

extern tchash_table_t *text_hash;

typedef struct {
    char *action;
    void *action_data;
    char *value;
    skin_t *skin;
} widget_data_t;


#endif /* _TCVPX_H */

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

#ifndef _TCVPX_H
#define _TCVPX_H

#include "tcvpx_tc2.h"
#include <tchash.h>

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
    window_t *window;
    int state;
    hash_table *id_hash;
    list *templates;
} skin_t;

extern skin_t* load_skin(char *skinfile);
extern int create_ui(window_t *win, skin_t *skin, tcconf_section_t *config, 
	      hash_table *parameters);

extern int init_skins(void);
extern void cleanup_skins(void);

extern int init_dynamic(void);
extern void free_dynamic(void);

extern int init_events(void);

extern int parse_text(char *text, char *result);
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

extern int quit;

extern player_t *pl;
extern eventq_t qs;
extern eventq_t qr;

extern int s_time;
extern int s_length;
hash_table *text_hash;

typedef struct {
    char *action;
    void *action_data;
    char *value;
    skin_t *skin;
} widget_data_t;


#endif /* _TCVPX_H */

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

typedef struct {
    conf_section *config;
    char *file;
    char *path;
    int width, height;
    window_t *window;
} skin_t;

skin_t* load_skin(char *skinfile);
int create_ui(skin_t *skin);

int register_actions();

int init_dynamic();

int parse_text(char *text, char *result);
int parse_variable(char *text, void **result);

int change_text(char *key, char *text);
int change_variable(char *key, void *data);

int unregister_textwidget(xtk_widget_t *w, char *text);
int register_textwidget(xtk_widget_t *w, char *text);

int unregister_varwidget(xtk_widget_t *w, char *text);
int register_varwidget(xtk_widget_t *w, char *text);

int update_time();
int update_state(char *state);

void *tcvp_event(void *p);
int toggle_time(xtk_widget_t *w, void *p);

extern int quit;

extern player_t *pl;
extern eventq_t qs;
extern eventq_t qr;

extern int s_time;
extern int s_length;
hash_table *text_hash;

typedef struct {
    char *action;
    void *data;
    skin_t *skin;
} action_data_t;


#endif /* _TCVPX_H */

/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#ifndef _TCVPCTL_H
#define _TCVPCTL_H

#include "tcvpx.h"

int tcvp_quit();
int tcvp_previous(xtk_widget_t *w, void *p);
int tcvp_next(xtk_widget_t *w, void *p);
int tcvp_play(xtk_widget_t *w, void *p);
int tcvp_stop(xtk_widget_t *w, void *p);
int tcvp_pause(xtk_widget_t *w, void *p);
int tcvp_seek(xtk_widget_t *w, void *p);
int tcvp_seek_rel(xtk_widget_t *w, void *p);

int tcvp_add_file(char *file);

#endif /* _TCVPCTL_H */

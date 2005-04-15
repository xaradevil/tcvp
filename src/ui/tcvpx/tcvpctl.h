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
int tcvp_playlist_remove(xtk_widget_t *w, void *p);
int tcvp_add_file(char *file);
int tcvp_playlist_query(xtk_widget_t *w, void *p);

#endif /* _TCVPCTL_H */

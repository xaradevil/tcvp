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

int tcvp_add_file(char *file);

#endif /* _TCVPCTL_H */

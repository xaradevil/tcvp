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

#include "widgets.h"

int tcvp_close(tcwidget_t *p, XEvent *e);
int tcvp_previous(tcwidget_t *p, XEvent *e);
int tcvp_next(tcwidget_t *p, XEvent *e);
int tcvp_play(tcwidget_t *p, XEvent *e);
int tcvp_stop(tcwidget_t *p, XEvent *e);
int tcvp_pause(tcwidget_t *p, XEvent *e);

#endif /* _TCVPCTL_H */

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

#ifndef _TCSKIN_H
#define _TCSKIN_H

#include "widgets.h"
#include <tcconf.h>

struct _skin_t {
    tclabel_t *time, *title;
    tcbackground_t *background;
    tcseek_bar_t *seek_bar;
    tcimage_button_t *close;
    tcimage_button_t *playctl[5];
    int width, height;
    char *file;
    char *path;
    conf_section *config;
};

skin_t* load_skin(char *skinfile);
int create_ui(skin_t *skin);

#endif /* _TCSKIN_H */

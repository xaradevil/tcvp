/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

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

typedef struct {
    int x;
    int y;
} pos_t;

struct _skin_t {
    pos_t pbg_pos, pcpos, closepos;
    tcimage_button_t *playctl[5], *close;
    tclabel_t *time, *title;
    tcbackground_t *background;
    int width, height;
    char *path;
};

#endif /* _TCSKIN_H */
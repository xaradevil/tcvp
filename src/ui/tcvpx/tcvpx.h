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

#ifndef _TCVPX_H
#define _TCVPX_H

#include "skin.h"

#define STOPPED 0
#define PLAYING 1

#define TCTIME_ELAPSED   0
#define TCTIME_REMAINING 1


int create_window(skin_t *);
int update_time(skin_t *skin);

void *x11_event(void *p);
void *tcvp_event(void *p);
void *scroll_labels(void *p);

extern int mapped;
extern int quit;
extern Display *xd;
extern int xs;
extern Window xw;
extern GC wgc, bgc;
extern Pixmap root;
extern int root_width;
extern int root_height;
extern int depth;

extern player_t *pl;
extern eventq_t qs;
extern eventq_t qr;

extern list *files;
extern list_item *flist_curr;
extern char *current_file;
extern int s_time;
extern int s_length;
extern int p_state;



#endif /* _TCVPX_H */
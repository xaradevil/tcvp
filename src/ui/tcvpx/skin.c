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

#include <tcconf.h>
#include "tcvpx.h"
#include <string.h>
#include "tcvpctl.h"
#include <X11/Xlib.h>

extern skin_t*
load_skin(char *skinconf)
{
    char *tmp;
    skin_t *skin = calloc(sizeof(skin_t), 1);

    if(!(skin->config = conf_load_file (NULL, skinconf))){
	fprintf(stderr, "Error loading file.\n");
	exit(1);
    }

    skin->file = skinconf;
    skin->path = strdup(skinconf);
    tmp = strrchr(skin->path, '/');
    if(tmp == NULL) {
	free(skin->path);
	skin->path = strdup("");	
    } else {
	*tmp=0;
    }

/*     if(conf_getvalue(skin->config, "name", "%s", &tmp) == 1) */
/* 	printf("Loaded skin: \"%s\"\n", tmp); */

    /* FIXME: do error checking */
    conf_getvalue(skin->config, "width", "%d", &skin->width);
    conf_getvalue(skin->config, "height", "%d", &skin->height);

    return skin;
}


static tcbackground_t*
create_skinned_background(skin_t *skin, conf_section *sec)
{
    char *file;
    conf_getvalue(sec, "background", "%s", &file);
    return(create_background(skin, file));
}

static tcimage_button_t*
create_skinned_button(skin_t *skin, conf_section *sec, onclick_cb_t ocb)
{
    char *file;
    int x, y;

    conf_getvalue(sec, "image", "%s", &file);
    conf_getvalue(sec, "position", "%d %d", &x, &y);

    return(create_button(skin, x, y, file, ocb));
}

static tclabel_t*
create_skinned_label(skin_t *skin, conf_section *sec, char *text,
		     onclick_cb_t ocb)
{
    int x, y;
    int w, h;
    int xoff, yoff;
    char *font;
    char *color;
    int alpha;
    int stype;

    conf_getvalue(sec, "position", "%d %d", &x, &y);
    conf_getvalue(sec, "size", "%d %d", &w, &h);
    conf_getvalue(sec, "text_offset", "%d %d", &xoff, &yoff);
    conf_getvalue(sec, "font", "%s", &font);
    if(conf_getvalue(sec, "color", "%s %d", &color, &alpha)==1){
	alpha = 0xff;
    }
    conf_getvalue(sec, "scroll", "%d", &stype);

    return(create_label(skin, x, y, w, h, xoff, yoff, text, font,
			color, alpha, stype, ocb));
}

extern int
create_ui(skin_t *skin)
{
    conf_section *sec;

    skin->background = create_skinned_background(skin, skin->config);

    sec = conf_getsection(skin->config, "buttons/previous");
    if(sec){
	create_skinned_button(skin, sec, tcvp_previous);
    }

    sec = conf_getsection(skin->config, "buttons/play");
    if(sec){
	create_skinned_button(skin, sec, tcvp_play);
    }

    sec = conf_getsection(skin->config, "buttons/pause");
    if(sec){
	create_skinned_button(skin, sec, tcvp_pause);
    }

    sec = conf_getsection(skin->config, "buttons/stop");
    if(sec){
	create_skinned_button(skin, sec, tcvp_stop);
    }

    sec = conf_getsection(skin->config, "buttons/next");
    if(sec){
	create_skinned_button(skin, sec, tcvp_next);
    }

    sec = conf_getsection(skin->config, "buttons/quit");
    if(sec){
	create_skinned_button(skin, sec, tcvp_close);
    }

    sec = conf_getsection(skin->config, "time");
    if(sec){
	skin->time = create_skinned_label(skin, sec, "  0:00", toggle_time);
    }

    sec = conf_getsection(skin->config, "title");
    if(sec){
	skin->title = create_skinned_label(skin, sec, "", NULL);
    }

    return 0;
}


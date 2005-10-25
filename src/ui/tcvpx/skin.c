/**
    Copyright (C) 2003 - 2005  Michael Ahlberg, Måns Rullgård

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

#include "tcvpx.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tcvpctl.h"

#define rwidth (*xtk_display_width)
#define rheight (*xtk_display_height)

static skin_t *tcvp_open_ui(xtk_widget_t *w, void *p);
static int tcvp_close_ui(xtk_widget_t *w, void *p);
static int tcvp_replace_ui(xtk_widget_t *w, void *p);
static int tcvp_sticky(xtk_widget_t *w, void *p);
static int tcvp_on_top(xtk_widget_t *w, void *p);
static int set_var(xtk_widget_t *w, void *p);
static int set_text(xtk_widget_t *w, void *p);
static int enable_widgets(xtk_widget_t *w, void *p);
static int disable_widgets(xtk_widget_t *w, void *p);

int ui_count = 1;
tchash_table_t *action_hash;

typedef struct {
    char *name;
    char *fmt;
    union {
	int integer;
	char* string;
    } value;
} parameter_t;


extern image_t *
load_image(char *skinpath, char *file)
{
    image_params_t ip;
    char fn[1024];
    image_t *img;
    url_t *u;

    if(!file)
	return NULL;

    snprintf(fn, 1023, "%s/%s", skinpath, file);
    u = url_open(fn, "r");
    if(!u)
	return NULL;
    ip.pixel_type = IMAGE_COLOR_RGB | IMAGE_COLOR_ALPHA;
    img = image_read(u, &ip);
    u->close(u);

    return img;
}


extern int
init_skins(void)
{
    typedef struct {
	char *name;
	action_cb_t action;
    } saction_t;

    saction_t actions[] = {
	{"play", tcvp_play},
	{"stop", tcvp_stop},
	{"pause", tcvp_pause},
	{"previous", tcvp_previous},
	{"next", tcvp_next},
	{"open_ui", (action_cb_t)tcvp_open_ui},
	{"close_ui", tcvp_close_ui},
	{"replace_ui", tcvp_replace_ui},
	{"toggle_time", toggle_time},
	{"seek", tcvp_seek},
	{"seek_relative", tcvp_seek_rel},
	{"on_top", tcvp_on_top},
	{"sticky", tcvp_sticky},
	{"set_variable", set_var},
	{"set_text", set_text},
	{"enable_widgets", enable_widgets},
	{"disable_widgets", disable_widgets},
	{"playlist_remove", tcvp_playlist_remove},
	{"playlist_query", tcvp_playlist_query},
	{NULL, NULL}
    };
    int i;

    action_hash = tchash_new(10, 0, 0);

    for(i=0; actions[i].name != NULL; i++) {
	tchash_search(action_hash, actions[i].name, -1, actions[i].action, NULL);
    }

    return 0;
}


extern void
cleanup_skins(void)
{
    if(action_hash)
	tchash_destroy(action_hash, NULL);
}


extern int
lookup_action(xtk_widget_t *w, void *p)
{
    action_cb_t acb = NULL;
    widget_data_t *wd;
    char *ac_c, *c;

    wd = xtk_widget_get_data(w);

    if(wd && wd->action) {
	tc2_print("TCVPX", TC2_PRINT_DEBUG+5, "Looking up action \"%s\"\n",wd->action);

	ac_c = c = strdup(wd->action);

	while(c != NULL) {
	    char *next, *param;

	    param = strchr(c, '(');
	    next = strchr(c, ',');

	    if(param < next || (!next && param)) {
		char *tmp;
		param[0] = 0;
		wd->action_data = param+1;
		tmp = strchr(param+1, ')');
		if(tmp) {
		    tmp[0] = 0;
		} else {
		    tc2_print("TCVPX", TC2_PRINT_ERROR, "Syntax error in skin config file\n");
		}
	    }

	    if(next) {
		next[0] = 0;
		next++;
	    }

	    tchash_find(action_hash, c, -1, &acb);
	    if(acb) {
		tc2_print("TCVPX", TC2_PRINT_DEBUG+2, "Action: \"%s(%s)\"\n", c, wd->action_data);
		acb(w, p);
	    } else {
		tc2_print("TCVPX", TC2_PRINT_WARNING,
			  "Action: \"%s\" not implemented\n", c);
	    }
	    c = next;
	}

	free(ac_c);
    }

    return 0;
}


static void
save_varcb(xtk_widget_t *w, action_cb_t cb, char *datatype, char *value)
{
    widget_data_t *wd = xtk_widget_get_data(w);

    wd->nvalues++;

    wd->values = realloc(wd->values, wd->nvalues * sizeof(*wd->values)); 
    wd->cbs = realloc(wd->cbs, wd->nvalues * sizeof(*wd->cbs));
    wd->datatypes = realloc(wd->datatypes,
			    wd->nvalues * sizeof(*wd->datatypes));

    wd->values[wd->nvalues-1] = strdup(value);
    wd->cbs[wd->nvalues-1] = cb;
    wd->datatypes[wd->nvalues-1] = strdup(datatype);
}


static void
free_varcb(xtk_widget_t *w)
{
    int i;
    widget_data_t *wd = xtk_widget_get_data(w);

    for(i=0; i<wd->nvalues; i++) {
	unregister_varwidget(w, wd->cbs[i], wd->datatypes[i], wd->values[i]);
	free(wd->datatypes[i]);
	free(wd->values[i]);
    }

    free(wd->cbs);
    wd->cbs = NULL;

    free(wd->datatypes);
    wd->datatypes = NULL;

    free(wd->values);
    wd->values = NULL;

    wd->nvalues = 0;
}


extern skin_t*
load_skin(char *skinconf)
{
    char *tmp;
    skin_t *skin = calloc(sizeof(*skin), 1);
    int i=0;
    char *conf_tmp = NULL;
    struct stat stat_foo;

    if(skinconf[0] == '/' || stat(skinconf, &stat_foo) == 0) {
	conf_tmp = strdup(skinconf);
    } else if (tcvp_ui_tcvpx_conf_skinpath_count > 0){
	int i;
	for(i=0; i<tcvp_ui_tcvpx_conf_skinpath_count && conf_tmp == NULL; i++){
	    conf_tmp = malloc(strlen(tcvp_ui_tcvpx_conf_skinpath[i]) + 
			      strlen(skinconf) + 2);
	    sprintf(conf_tmp, "%s/%s", tcvp_ui_tcvpx_conf_skinpath[i],
		    skinconf);
	    if(stat(conf_tmp, &stat_foo) != 0) {
		free(conf_tmp);
		conf_tmp = NULL;
	    }
	}
    }

    if(conf_tmp == NULL) {
	conf_tmp = malloc(strlen(getenv("HOME"))+strlen(skinconf)+15);
	sprintf(conf_tmp, "%s/.tcvp/skins/%s", getenv("HOME"), skinconf);
	if(stat(conf_tmp, &stat_foo) != 0) {
	    free(conf_tmp);
	    conf_tmp = NULL;
	}
    }

    if(conf_tmp == NULL) {
	conf_tmp = malloc(strlen(TCVP_SKINS)+strlen(skinconf)+2);
	sprintf(conf_tmp, "%s/%s", TCVP_SKINS, skinconf);
	stat(conf_tmp, &stat_foo);
    }

    if(S_ISDIR(stat_foo.st_mode)) {
	tmp = conf_tmp;
	conf_tmp = malloc(strlen(tmp) + 11);
	sprintf(conf_tmp, "%s/skin.conf", tmp);
	free(tmp);
    }

    tc2_print("TCVPX", TC2_PRINT_DEBUG+4, "Using skin: \"%s\"\n", conf_tmp);

    if(!(skin->config = tcconf_load_file (NULL, conf_tmp))){
	tc2_print("TCVPX", TC2_PRINT_ERROR, "Error loading file \"%s\".\n", conf_tmp);
	free(conf_tmp);
	return NULL;
    }

    skin->file = conf_tmp;
    skin->path = strdup(conf_tmp);
    tmp = strrchr(skin->path, '/');
    if(!tmp){
	strcpy(skin->path, ".");
    } else {
	*tmp = 0;
    }

    tcconf_getvalue(skin->config, "doubleclick_action", "%s", &skin->dblclick);

/*     if(tcconf_getvalue(skin->config, "name", "%s", &tmp) == 1) */
/* 	printf("Loaded skin: \"%s\"\n", tmp); */

    i = tcconf_getvalue(skin->config, "size", "%d %d", &skin->width,
			&skin->height);
    if(i != 2) {
	free(skin);
	return NULL;
    }

    skin->id_hash = tchash_new(10, 0, 0);

    return skin;
}


static void
free_skin(skin_t *skin)
{
    free(skin->file);
    free(skin->path);
    tcfree(skin->config);
    tchash_destroy(skin->id_hash, NULL);
    free(skin);
}


static xtk_widget_t*
create_skinned_background(xtk_widget_t *c, skin_t *skin,
			  tcconf_section_t *sec, tchash_table_t *parameters)
{
    char *file = NULL;
    int i = 0;
    image_t *img = NULL;
    xtk_widget_t *w;

    i += tcconf_getvalue(sec, "background", "%s", &file);
    img = load_image(skin->path, file);

    if(img)
	i++;

    if(i != 2)
	return NULL;

    w = xtk_widget_image_create(c, 0, 0, c->width, c->height);
    xtk_widget_image_set_image(w, img);
    xtk_widget_show(w);

    xtk_widget_container_set_shape(c, img);

    free(file);
    tcfree(img);

    return (xtk_widget_t*)c;
}


static int
destroy_skinned_box(xtk_widget_t *w)
{
    void *d = xtk_widget_get_data(w);
    if(d) {
	free(d);
    }
    return 0;
}

static xtk_widget_t*
create_skinned_box(xtk_widget_t *c, skin_t *skin, tcconf_section_t *sec,
		   tchash_table_t *parameters)
{
    int x, y, width, height;
    widget_data_t *wd;
    int i=0;

    i += tcconf_getvalue(sec, "position", "%d %d", &x, &y);
    i += tcconf_getvalue(sec, "size", "%d %d", &width, &height);

    if(i != 4){
	return NULL;
    }

    xtk_widget_t *w = xtk_widget_container_create(c, x, y, width, height);
    wd = calloc(sizeof(*wd), 1);
    wd->skin = skin;
    xtk_widget_container_set_data(w, wd);
    w->on_destroy = destroy_skinned_box;

    create_ui(w, skin, sec, parameters);

    return w;
}


static int
destroy_skinned_button(xtk_widget_t *w)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    if(wd) {
	if(wd->action) free(wd->action);
	free(wd);
    }
    return 0;
}

static xtk_widget_t*
create_skinned_button(xtk_widget_t *c, skin_t *skin, tcconf_section_t *sec,
		      tchash_table_t *parameters)
{
    char *file = NULL, *of = NULL, *df = NULL, *bg = NULL;
    int x, y;
    int i=0;
    widget_data_t *wd = calloc(sizeof(*wd), 1);
    xtk_widget_t *bt;
    image_t *img;
    int shaped = 0;

    i += tcconf_getvalue(sec, "action", "%s", &wd->action);
    i += tcconf_getvalue(sec, "image", "%s", &file);
    i += tcconf_getvalue(sec, "position", "%d %d", &x, &y);

    if(i != 4){
	return NULL;
    }

    tcconf_getvalue(sec, "shaped", "%d", &shaped);

    tcconf_getvalue(sec, "mouse_over", "%s", &of);
    tcconf_getvalue(sec, "pressed", "%s", &df);
    wd->skin = skin;
/*     tcconf_getvalue(sec, "background", "%s", &bg); */

    if(!(img = load_image(skin->path, file)))
	return NULL;

    bt = xtk_widget_button_create(c, x, y, img->params.width[0],
				  img->params.height[0]);
    xtk_widget_button_set_image(bt, img);
    if(shaped)
	xtk_widget_button_set_shape(bt, img);
    tcfree(img);

    if((img = load_image(skin->path, of))){
	xtk_widget_button_set_hover_image(bt, img);
	if(shaped)
	    xtk_widget_button_set_hover_shape(bt, img);
	tcfree(img);
    }

    if((img = load_image(skin->path, df))){
	xtk_widget_button_set_pressed_image(bt, img);
	if(shaped)
	    xtk_widget_button_set_pressed_shape(bt, img);
	tcfree(img);
    }

    xtk_widget_button_set_data(bt, wd);
    xtk_widget_button_set_action(bt, lookup_action);

    bt->on_destroy = destroy_skinned_button;

    free(file);
    free(of);
    free(df);
    free(bg);

    return bt;
}


static int
destroy_skinned_label(xtk_widget_t *w)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    if(wd) {
	unregister_textwidget(w, wd->value);
	free(wd->value);
	if(wd->action) free(wd->action);
	free(wd);
    }
    return 0;
}

static xtk_widget_t*
create_skinned_label(xtk_widget_t *win, skin_t *skin, tcconf_section_t *sec,
		     tchash_table_t *parameters)
{
    int x, y;
    int width, height;
    int xoff = 0, yoff = 0;
    char *font;
    char *color, *align_s, *stype_s;
    int alpha = 0xff;
    int stype, align;
    int i = 0;
    char *action = NULL, *bg = NULL, *text, *default_text;
    widget_data_t *wd = calloc(sizeof(*wd), 1);
    xtk_widget_t *l, *w;
    tcconf_section_t *bfnt = NULL;
    image_t *bg_img;

    i += tcconf_getvalue(sec, "position", "%d %d", &x, &y);
    i += tcconf_getvalue(sec, "size", "%d %d", &width, &height);
    i += tcconf_getvalue(sec, "text", "%s", &text);

    if((bfnt = tcconf_getsection(sec, "bitmap"))){
	tcconf_setvalue(bfnt, "path", "%s", skin->path);
	i++;
    } else if(tcconf_getvalue(sec, "font", "%s", &font) == 1){
	i++;
    }

    i += tcconf_getvalue(sec, "color", "%s %d", &color, &alpha) > 0;

    if(tcconf_getvalue(sec, "scroll_style", "%s", &stype_s) < 1)
	stype_s = strdup("none");

    if(tcconf_getvalue(sec, "align", "%s", &align_s) < 1)
	align_s = strdup("left");


    if(strcasecmp(align_s, "left") == 0) {
	align = XTKLABELLEFT;
    } else if(strcasecmp(align_s, "right") == 0) {
	align = XTKLABELRIGHT;
    } else {
	align = XTKLABELCENTER;
    }
    free(align_s);

    if(strcasecmp(stype_s, "scroll") == 0) {
	stype = XTKLABELSCROLL;
    } else if(strcasecmp(stype_s, "pingpong") == 0) {
	stype = XTKLABELPINGPONG;
    } else {
	stype = 0;
    }
    free(stype_s);

    if(i != 7)
	return NULL;

    tcconf_getvalue(sec, "text_offset", "%d %d", &xoff, &yoff);
    tcconf_getvalue(sec, "action", "%s", &action);
    tcconf_getvalue(sec, "background", "%s", &bg);

    default_text = malloc(1024);
    parse_text(text, default_text, 1024);

    wd->action = action;
    wd->value = text;
    wd->skin = skin;
    l = xtk_widget_label_create(win, x, y, width, height);
    if(bfnt){
	xtk_widget_label_set_bitmapfont(l, bfnt);
	tcfree(bfnt);
    } else {
	xtk_widget_label_set_font(l, font);
	free(font);
    }
    xtk_widget_label_set_offset(l, xoff, yoff);
    xtk_widget_label_set_color(l, color, alpha);
    xtk_widget_label_set_text(l, default_text);
    xtk_widget_label_set_data(l, wd);
    xtk_widget_label_set_action(l, lookup_action);
    xtk_widget_label_set_align(l, align);
    xtk_widget_label_set_scroll(l, stype);

    bg_img = load_image(skin->path, bg);
    w = xtk_widget_image_create(win, x, y, width, height);
    xtk_widget_image_set_image(w, bg_img);
    tcfree(bg_img);

    xtk_widget_show(w);

    register_textwidget(l, text);
    l->on_destroy = destroy_skinned_label;
    free(default_text);
    if(color)
	free(color);
    free(bg);

    return l;
}


static int
seek_bar_update(xtk_widget_t *w, void *data)
{
    XTK_SLIDER(w, s);
    if(s) {
	double pos = (data)?*((double *)data):0;
	if(pos < 0) {
	    xtk_widget_disable(w);
	} else {
	    xtk_widget_enable(w);
	}
	return xtk_widget_slider_set_position(w, pos);
    }

    return -1;
}

static int
destroy_skinned_seek_bar(xtk_widget_t *w)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    if(wd) {
	unregister_varwidget(w, seek_bar_update, "double", wd->value);
	free(wd->value);
	if(wd->action) free(wd->action);
	free(wd);
    }
    return 0;
}

static xtk_widget_t*
create_skinned_seek_bar(xtk_widget_t *win, skin_t *skin, tcconf_section_t *sec,
			tchash_table_t *parameters)
{
    int x, y;
    int sp_x, sp_y;
    int ep_x, ep_y;
    int xd=0, yd=0, sx=0, sy=0;
    char *bg, *indicator, *value, *variable, *ind_over = NULL,
	*ind_down = NULL;
    int i=0;
    char *action = NULL;
    widget_data_t *wd = calloc(sizeof(*wd), 1);
    double *position = NULL, *def = NULL, p=0;
    xtk_widget_t *s;
    int disable = 0;
    image_t *img;

    i += tcconf_getvalue(sec, "position", "%d %d", &x, &y);
    i += tcconf_getvalue(sec, "start_position", "%d %d", &sp_x, &sp_y);
    i += tcconf_getvalue(sec, "end_position", "%d %d", &ep_x, &ep_y);
    i += tcconf_getvalue(sec, "background", "%s", &bg);
    i += tcconf_getvalue(sec, "indicator", "%s", &indicator);
    i += tcconf_getvalue(sec, "value", "%s %s", &variable, &value);

    if(i != 10){
	return NULL;
    }

    tcconf_getvalue(sec, "action", "%s", &action);
    tcconf_getvalue(sec, "mouse_over", "%s", &ind_over);
    tcconf_getvalue(sec, "pressed", "%s", &ind_down);
    tcconf_getvalue(sec, "scroll_direction", "%d %d %d %d", &xd, &yd,
		    &sx, &sy);

    parse_variable(value, (void *)&position, (void *)&def);
    if(!position) {
	if(def) {
	    p = *def;
	    tcfree(def);
	}
	position = &p;
    } else if(*position < 0 || *position > 1) {
	disable = 1;
    }

    wd->action = action;
    wd->value = value;
    wd->skin = skin;

    img = load_image(skin->path, bg);
    s = xtk_widget_slider_create(win, x, y, img->params.width[0],
				 img->params.height[0]);
    xtk_widget_slider_set_image(s, img);
    tcfree(img);

    xtk_widget_slider_set_data(s, wd);
    xtk_widget_slider_set_action(s, lookup_action);
    xtk_widget_slider_set_position(s, *position);
    xtk_widget_slider_set_bounds(s, sp_x, sp_y, ep_x, ep_y);
    xtk_widget_slider_set_scroll_direction(s, xd, yd, sx, sy);

    img = load_image(skin->path, indicator);
    xtk_widget_slider_set_indicator_image(s, img);
    tcfree(img);

    img = load_image(skin->path, ind_over);
    xtk_widget_slider_set_indicator_image_hover(s, img);
    if(img)
	tcfree(img);

    img = load_image(skin->path, ind_down);
    xtk_widget_slider_set_indicator_image_pressed(s, img);
    if(img)
	tcfree(img);

    if(s) {
	if(strcmp(variable, "position") == 0) {
	    register_varwidget((xtk_widget_t *)s, seek_bar_update,
			       "double", wd->value);
	}
	free(variable);
	s->on_destroy = destroy_skinned_seek_bar;
	if(disable) xtk_widget_disable(s);
    }

    free(ind_over);
    free(ind_down);
    free(indicator);
    free(bg);

    return s;
}


static int
list_set_current(xtk_widget_t *w, void *data)
{
/*     printf("Current: %d\n", *((int*)data)); */

    xtk_widget_list_set_current(w, *((int*)data));

    return 0;
}

static int
list_set_number_of_entries(xtk_widget_t *w, void *data)
{
/*     printf("Number of entries: %d\n", *((int*)data)); */

    return 0;
}

static int
list_set_entries(xtk_widget_t *w, void *data)
{
/*     int i; */
    char **entries = (char**) data;

/*     printf("List entries\n"); */
/*     for(i=0; entries[i] != NULL; i++) { */
/* 	printf("  %s\n", entries[i]); */
/*     } */

    xtk_widget_list_set_entries(w, entries);

    return 0;
}

static int
destroy_skinned_list(xtk_widget_t *w)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    if(wd) {
	free_varcb(w);
	if(wd->action) free(wd->action);
	free(wd);
    }
    return 0;
}

static xtk_widget_t*
create_skinned_list(xtk_widget_t *win, skin_t *skin, tcconf_section_t *sec,
		    tchash_table_t *parameters)
{
    int x, y;
    int width, height;
    int i=0;
    char *action = NULL;
    widget_data_t *wd = calloc(sizeof(*wd), 1);
    xtk_widget_t *l;
    int disable = 0;
    int alpha = 0xff;
    char *font, *color;
    tcconf_section_t *bfnt = NULL;
    tcconf_section_t *fig = NULL;
    int rows, spacing;
    int xs, ys;

    i += tcconf_getvalue(sec, "position", "%d %d", &x, &y);
    i += tcconf_getvalue(sec, "size", "%d %d", &width, &height);

    if((bfnt = tcconf_getsection(sec, "bitmap"))){
	tcconf_setvalue(bfnt, "path", "%s", skin->path);
	i++;
    } else if(tcconf_getvalue(sec, "font", "%s", &font) == 1){
	i++;
    }

    i += tcconf_getvalue(sec, "color", "%s %d", &color, &alpha) > 0;
    i += tcconf_getvalue(sec, "spacing", "%d", &spacing);
    i += tcconf_getvalue(sec, "rows", "%d", &rows);

    if(i != 8){
	return NULL;
    }

    fig = tcconf_getsection(sec, "background_figure");

    tcconf_getvalue(sec, "action", "%s", &action);

    wd->action = action;
    wd->skin = skin;
    wd->values = calloc(1, sizeof(char *));

    l = xtk_widget_list_create(win, x, y, width, height);

    if(tcconf_getvalue(sec, "scroll", "%d %d", &xs, &ys) == 2) {
        xtk_widget_list_set_scroll(l, xs, ys);
    }

    if(fig != NULL) {
	tcconf_setvalue(fig, "position", "%d %d", 0, 0);
	tcconf_setvalue(fig, "size", "%d %d", width, height);

	image_t *img = draw_figure(fig);

	xtk_widget_list_set_image(l, img);
    }

    xtk_widget_list_set_data(l, wd);
    xtk_widget_list_set_action(l, lookup_action);

    if(l) {
	void *c = NULL;
	char *value, *variable;

	if(bfnt){
	    xtk_widget_list_set_bitmapfont(l, bfnt);
	    tcfree(bfnt);
	} else {
	    xtk_widget_list_set_font(l, font);
	    free(font);
	}

	xtk_widget_list_set_color(l, color, alpha);
	free(color);

	xtk_widget_list_set_spacing(l, spacing);

	xtk_widget_list_set_rows(l, rows);

	while(i = tcconf_nextvalue(sec, "value", &c, "%s %s",
				   &variable, &value), c) {
	    if(i == 2) {
		if(strcmp(variable, "current_position") == 0) {
		    int *val = NULL, *def = NULL;
		    int p;

		    save_varcb(l, list_set_current, "integer", value);
		    register_varwidget(l, list_set_current, "integer",
				       value);

		    parse_variable(value, (void *)&val, (void *)&def);
		    if(!val) {
			if(def) {
			    p = *def;
			    tcfree(def);
			}
			val = &p;
		    }

		    list_set_current(l, val);
		} else if(strcmp(variable, "number_of_entries") == 0) {
		    int *val = NULL, *def = NULL;
		    int p;

		    save_varcb(l, list_set_current, "integer", value);
		    register_varwidget(l, list_set_number_of_entries,
				       "integer", value);

		    parse_variable(value, (void *)&val, (void *)&def);
		    if(!val) {
			if(def) {
			    p = *def;
			    tcfree(def);
			}
			val = &p;
		    }

		    list_set_number_of_entries(l, val);
		} else if(strcmp(variable, "entries") == 0) {
		    void *val = NULL;

		    save_varcb(l, list_set_current, "string_array", value);
		    register_varwidget(l, list_set_entries,
				       "string_array", value);

		    parse_variable(value, &val, NULL);
		    if(val) list_set_entries(l, val);
		}
		free(value);
		free(variable);
	    }
	}

	l->on_destroy = destroy_skinned_list;
	if(disable) xtk_widget_disable(l);
    }

    return l;
}


static int
destroy_skinned_state(xtk_widget_t *w)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    if(wd) {
	unregister_textwidget(w, wd->value);
	free(wd->value);
	if(wd->action) free(wd->action);
	free(wd);
    }
    return 0;
}

static xtk_widget_t*
create_skinned_state(xtk_widget_t *win, skin_t *skin, tcconf_section_t *sec,
		     tchash_table_t *parameters)
{
    int x, y;
    int ns = 0;
    image_t **imgs = NULL;
    char **states = NULL;
    void *c = NULL;
    int i;
    char *img, *st, def_state[512], *bg = NULL;
    widget_data_t *wd = calloc(sizeof(*wd), 1);
    xtk_widget_t *s = NULL;

    i = tcconf_getvalue(sec, "position", "%d %d", &x, &y);
    i += tcconf_getvalue(sec, "value", "%s", &wd->value);
    if (i != 3) {
	return NULL;
    }

    while(i = tcconf_nextvalue(sec, "image", &c, "%s %s", &st, &img), c) {
	if(i == 2) {
	    imgs = realloc(imgs, sizeof(*imgs)*(ns+1));
	    states = realloc(states, sizeof(*states)*(ns+1));
	    imgs[ns] = load_image(skin->path, img);
	    states[ns] = st;
	    ns++;
	    free(img);
	}
    }

    tcconf_getvalue(sec, "action", "%s", &wd->action);
    wd->skin = skin;

    parse_text(wd->value, def_state, 512);

    if(ns > 0) {
	s = xtk_widget_state_create(win, x, y, imgs[0]->params.width[0],
				    imgs[0]->params.height[0]);
	for(i=0; i<ns; i++) {
	    xtk_widget_state_add_state(s, states[i], imgs[i]);
	    tcfree(imgs[i]);
	}
	xtk_widget_state_set_state(s, def_state);

	xtk_widget_state_set_data(s, wd);
	xtk_widget_state_set_action(s, lookup_action);

	register_textwidget((xtk_widget_t *)s, wd->value);
 	s->on_destroy = destroy_skinned_state;
    }

    free(bg);
    if(imgs)
	free(imgs);
    if(states){
	for(i = 0; i < ns; i++)
	    free(states[i]);
	free(states);
    }

    return s;
}
    

static int
tcvp_replace_ui(xtk_widget_t *w, void *p)
{
    skin_t *s;
    if((s = tcvp_open_ui(w, p)) != NULL) {
	widget_data_t *wd = xtk_widget_get_data(w);
	skin_t *os = wd->skin;
	xtk_position_t *pos = xtk_window_get_position(os->window);
	if(pos) {
	    if(pos->x + s->width > rwidth || pos->x + os->width == rwidth) {
		pos->x = rwidth - s->width;
	    }
	    if(pos->y + s->height > rheight || pos->y + os->height == rheight){
		pos->y = rheight - s->height;
	    }
	    xtk_window_set_position(s->window, pos);
	    free(pos);
	}
	tcvp_close_ui(w, p);
    }
    return 0;
}

static int
tcvp_close_ui(xtk_widget_t *w, void *p)
{
    widget_data_t *wd;
    widget_data_t *owd = xtk_widget_get_data(w);
    skin_t *s = owd->skin;
    xtk_widget_t *win = s->window;

    wd = xtk_widget_get_data(win);

    unregister_textwidget(win, wd->value);
    if(wd->action) free(wd->action);
    free(wd);

    xtk_window_destroy(win);
    free_skin(s);

    ui_count--;
    if(ui_count == 0) {
	tcvp_quit();
    }

    return 0;
}

static skin_t*
tcvp_open_ui(xtk_widget_t *w, void *p)
{
    char *buf;
    widget_data_t *wd;
    
    widget_data_t *owd = xtk_widget_get_data(w);

    char *uifile = owd->action_data; 
    skin_t *s = owd->skin;

    buf = malloc(strlen(uifile) + strlen(s->path) + 2);
    sprintf(buf, "%s/%s", s->path, uifile);

    skin_t *skin = load_skin(buf);
    if(!skin) {
	return NULL;
    }

    skin->window = xtk_window_create(NULL, 0, 0, skin->width, skin->height);
    xtk_window_set_dnd_callback(skin->window, tcvp_add_file);
    xtk_window_set_class(skin->window, "TCVP");

    if(create_ui(skin->window, skin, skin->config, NULL) != 0){
	tc2_print("TCVPX", TC2_PRINT_ERROR,
		  "Unable to load skin: \"%s\"\n", buf);
	return NULL;
    }

    free(buf);

    wd = calloc(sizeof(*wd), 1);
    wd->action = skin->dblclick;
    wd->skin = skin;
    xtk_widget_container_set_data(skin->window, wd);

    xtk_window_set_doubleclick_callback(skin->window, lookup_action);

    xtk_window_set_sticky_callback(skin->window, sticky_cb);
    xtk_window_set_on_top_callback(skin->window, on_top_cb);

    char *default_text = malloc(1024);
    wd->value = tcvp_ui_tcvpx_conf_window_title;
    register_textwidget(skin->window, wd->value);

    parse_text(wd->value, default_text, 1024);
    xtk_window_set_title(skin->window, default_text);
    free(default_text);

    xtk_window_show(skin->window);

    if((s->state & ST_STICKY) != 0) {
	xtk_window_set_sticky(skin->window, 1);
	skin->state |= ST_STICKY;
    }

    if((s->state & ST_ON_TOP) != 0) {
	xtk_window_set_always_on_top(skin->window, 1);
	skin->state |= ST_ON_TOP;
    }


    ui_count++;

    return skin;
}


#define create_skinned_widget(name, fn)					\
do {									\
    tcconf_section_t *sec;						\
    void *s = NULL, *w;							\
    while((sec = tcconf_nextsection(config, name, &s))){		\
	char *id = NULL;						\
	int e = 1;							\
									\
	w = fn(c, skin, sec, parameters);				\
	if(!w) {							\
	    tc2_print("TCVPX", TC2_PRINT_WARNING,			\
		      "Widget \"%s\", could not be created.\n",		\
		    name);						\
	} else {							\
	    tcconf_getvalue(sec, "id", "%s", &id);			\
	    if(id){							\
		tchash_replace(skin->id_hash, id, -1, w, NULL);	       	\
		free(id);						\
	    }								\
	    tcconf_getvalue(sec, "enabled", "%d", &e);			\
	    if(e != 0) xtk_widget_show(w);				\
	}								\
	tcfree(sec);							\
    }									\
} while(0);

extern int
create_ui(xtk_widget_t *c, skin_t *skin, tcconf_section_t *config,
	  tchash_table_t *parameters)
{
    create_skinned_background(c, skin, config, parameters);

    create_skinned_widget("box", create_skinned_box);
    create_skinned_widget("button", create_skinned_button);
    create_skinned_widget("label", create_skinned_label);
    create_skinned_widget("state", create_skinned_state);
    create_skinned_widget("slider", create_skinned_seek_bar);
    create_skinned_widget("list", create_skinned_list);

    return 0;
}


static int
tcvp_sticky(xtk_widget_t *w, void *p)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    char *d = wd->action_data;
    skin_t *s = wd->skin;

    if(!d || strcasecmp(d, "toggle") == 0) {
	if(s->state & ST_STICKY) {
	    xtk_window_set_sticky(s->window, 0);
	    s->state &= ~ST_STICKY;	
	} else {
	    xtk_window_set_sticky(s->window, 1);
	    s->state |= ST_STICKY;
	}
    } else if(strcasecmp(d, "set") == 0) {
	xtk_window_set_sticky(s->window, 1);
	s->state |= ST_STICKY;	
    } else if(strcasecmp(d, "unset") == 0) {
	xtk_window_set_sticky(s->window, 0);
	s->state &= ~ST_STICKY;
    }

    if(s->state & ST_STICKY) {
	change_text("sticky", "set");
    } else {
	change_text("sticky", "unset");
    }

    return 0;
}


static int
tcvp_on_top(xtk_widget_t *w, void *p)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    char *d = wd->action_data;
    skin_t *s = wd->skin;

    if(!d || strcasecmp(d, "toggle") == 0) {
	if(s->state & ST_ON_TOP) {
	    xtk_window_set_always_on_top(s->window, 0);
	    s->state &= ~ST_ON_TOP;	
	} else {
	    xtk_window_set_always_on_top(s->window, 1);
	    s->state |= ST_ON_TOP;
	}
    } else if(strcasecmp(d, "set") == 0) {
	xtk_window_set_always_on_top(s->window, 1);
	s->state |= ST_ON_TOP;	
    } else if(strcasecmp(d, "unset") == 0) {
	xtk_window_set_always_on_top(s->window, 0);
	s->state &= ~ST_ON_TOP;
    }

    if(s->state & ST_ON_TOP) {
	change_text("on_top", "set");
    } else {
	change_text("on_top", "unset");
    }

    return 0;
}


static int
set_var(xtk_widget_t *w, void *p)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    char *d = wd->action_data;

    if(d) {
	XTK_SLIDER(w, s);
	if(s) {
	    double *pos = tcalloc(sizeof(*pos));
	    *pos = *((double*)p);
	    change_variable(d, "double", pos);
	}
    }

    return 0;
}

static int
set_text(xtk_widget_t *w, void *p)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    char *d = wd->action_data;

    if(d) {
	tc2_print("TCVPX", TC2_PRINT_WARNING, "set_text(%s) not yet implemented\n", d);
    }

    return 0;
}


static int
show_widgets(skin_t *skin, char *widgets, int show)
{
    char *d = strdup(widgets);
    char *tmp = d;

    while(tmp != NULL) {
	char *next = strchr(tmp, ',');
	xtk_widget_t *w = NULL;

	if(next) {
	    next[0]=0;
	    next++;
	}
	
	tchash_find(skin->id_hash, tmp, -1, &w);
	if(w) {
	    if(show) {
		xtk_widget_show(w);
	    } else {
		xtk_widget_hide(w);
	    }
	    xtk_widget_repaint(w);
	    xtk_widget_draw(w);
	}
	tmp = next;
    }

    free(d);

    return 0;
}


static int
enable_widgets(xtk_widget_t *xw, void *p)
{
    widget_data_t *wd = xtk_widget_get_data(xw);

    if(wd) {
	show_widgets(wd->skin, strdup(wd->action_data), 1);
	return 0;
    }

    return -1;
}


static int
disable_widgets(xtk_widget_t *xw, void *p)
{
    widget_data_t *wd = xtk_widget_get_data(xw);

    if(wd) {
	show_widgets(wd->skin, wd->action_data, 0);
	return 0;
    }

    return -1;
}


extern int
on_top_cb(xtk_widget_t *w, int i)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    skin_t *s = wd->skin;

    if(i == 0) {
	s->state &= ~ST_ON_TOP;
	change_text("on_top", "unset");
    } else {
	s->state |= ST_ON_TOP;	
	change_text("on_top", "set");
    }

    return 0;
}


extern int
sticky_cb(xtk_widget_t *w, int i)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    skin_t *s = wd->skin;

    if(i == 0) {
	s->state &= ~ST_STICKY;
    } else {
	s->state |= ST_STICKY;	
    }

    return 0;
}

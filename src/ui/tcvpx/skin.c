/**
    Copyright (C) 2003, 2004  Michael Ahlberg, Måns Rullgård

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
#include "tcvpctl.h"
#include <X11/Xlib.h>

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
    tchash_destroy(action_hash, NULL);
}

extern int
lookup_action(xtk_widget_t *w, void *p)
{
    action_cb_t acb = NULL;
    widget_data_t *wd = (widget_data_t*)w->data;
    char *ac_c, *c;

    if(wd->action) {
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
		    fprintf(stderr, "Syntax error in skin config file\n");
		}
	    }

	    if(next) {
		next[0] = 0;
		next++;
	    }

	    tchash_find(action_hash, c, -1, &acb);
	    if(acb) {
/* 		fprintf(stderr, "Action: \"%s(%s)\"\n", c, wd->action_data); */
		acb(w, p);
	    } else {
		fprintf(stderr, "Action: \"%s\" not implemented\n", c);
	    }
	    c = next;
	}

	free(ac_c);
    }

    return 0;
}

extern skin_t*
load_skin(char *skinconf)
{
    char *tmp;
    skin_t *skin = calloc(sizeof(*skin), 1);
    int i=0;

    if(!(skin->config = tcconf_load_file (NULL, skinconf))){
	fprintf(stderr, "Error loading file \"%s\".\n", skinconf);
	return NULL;
    }

    skin->file = skinconf;
    skin->path = strdup(skinconf);
    tmp = strrchr(skin->path, '/');
    if(!tmp){
	strcpy(skin->path, ".");
    } else {
	*tmp = 0;
    }

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
    free(w->data);
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
    xtk_widget_container_set_data(c, wd);
    w->on_destroy = destroy_skinned_box;

    create_ui(w, skin, sec, parameters);

    return w;
}


static int
destroy_skinned_button(xtk_widget_t *w)
{
    widget_data_t *wd = (widget_data_t*)w->data;
    free(wd->action);
    free(wd);
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
    widget_data_t *wd = (widget_data_t*)w->data;
    unregister_textwidget(w, wd->value);
    free(wd->value);
    free(wd->action);
    free(wd);
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
    int alpha;
    int stype, align;
    int i=0, j;
    char *action = NULL, *bg = NULL, *text, *default_text;
    widget_data_t *wd = calloc(sizeof(*wd), 1);
    xtk_widget_t *l, *w;
    tcconf_section_t *bfnt = NULL;

    i += tcconf_getvalue(sec, "position", "%d %d", &x, &y);
    i += tcconf_getvalue(sec, "size", "%d %d", &width, &height);

    i += tcconf_getvalue(sec, "text", "%s", &text);

    if((bfnt = tcconf_getsection(sec, "bitmap"))){
	tcconf_setvalue(bfnt, "path", "%s", skin->path);
	i++;
    } else if(tcconf_getvalue(sec, "font", "%s", &font) == 1){
	i++;
    }

    if((j = tcconf_getvalue(sec, "color", "%s %d", &color, &alpha)) == 1){
	alpha = 0xff;
	j++;
    }
    i += j;
    if((j = tcconf_getvalue(sec, "scroll_style", "%s", &stype_s)) < 1){
	stype_s = strdup("none");
	j = 1;
    }
    i += j;
    if((j = tcconf_getvalue(sec, "align", "%s", &align_s)) < 1){
	j = 1;
	align_s = strdup("left");
    }
    i += j;
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

    if(i != 10){
	return NULL;
    }

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

    w = xtk_widget_image_create(win, x, y, width, height);
    xtk_widget_image_set_image(w, load_image(skin->path, bg));
    xtk_widget_show(w);

    register_textwidget(l, text);
    l->on_destroy = destroy_skinned_label;
    free(default_text);
    free(color);
    free(bg);

    return l;
}


static int
destroy_skinned_seek_bar(xtk_widget_t *w)
{
    widget_data_t *wd = (widget_data_t*)w->data;
    unregister_varwidget(w, wd->value);
    free(wd->value);
    free(wd->action);
    free(wd);
    return 0;
}

static xtk_widget_t*
create_skinned_seek_bar(xtk_widget_t *win, skin_t *skin, tcconf_section_t *sec,
			tchash_table_t *parameters)
{
    int x, y;
    int sp_x, sp_y;
    int ep_x, ep_y;
    char *bg, *indicator, *value, *ind_over = NULL, *ind_down = NULL;
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
    i += tcconf_getvalue(sec, "value", "%s", &value);

    if(i != 9){
	return NULL;
    }

    tcconf_getvalue(sec, "action", "%s", &action);
    tcconf_getvalue(sec, "mouse_over", "%s", &ind_over);
    tcconf_getvalue(sec, "pressed", "%s", &ind_down);

    parse_variable(value, (void *)&position, (void *)&def);
    if(!position) {
	if(def) {
	    p = *def;
	    free(def);
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
	register_varwidget((xtk_widget_t *)s, wd->value);
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
destroy_skinned_state(xtk_widget_t *w)
{
    widget_data_t *wd = (widget_data_t*)w->data;
    unregister_textwidget(w, wd->value);
    free(wd->value);
    free(wd->action);
    free(wd);
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

    while(i = tcconf_nextvalue(sec, "image", &c, "%s %s", &st, &img), c){
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
	skin_t *os = ((widget_data_t *)w->data)->skin;
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
    skin_t *s = ((widget_data_t *)w->data)->skin;
    xtk_widget_t *win = s->window;

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
    char *uifile = ((widget_data_t *)w->data)->action_data; 
    skin_t *s = ((widget_data_t *)w->data)->skin;

    buf = malloc(strlen(uifile) + strlen(s->path) + 2);
    sprintf(buf, "%s/%s", s->path, uifile);

    skin_t *skin = load_skin(buf);
    if(!skin) {
	return NULL;
    }

    skin->window = xtk_window_create(NULL, 0, 0, skin->width, skin->height);
    xtk_window_set_dnd_callback(skin->window, tcvp_add_file);

    create_ui(skin->window, skin, skin->config, NULL);

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
	    fprintf(stderr, "Widget \"%s\", could not be created.\n",	\
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

    return 0;
}


static int
tcvp_sticky(xtk_widget_t *w, void *p)
{
    char *d = ((widget_data_t *)w->data)->action_data;
    skin_t *s = ((widget_data_t *)w->data)->skin;

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
    char *d = ((widget_data_t *)w->data)->action_data;
    skin_t *s = ((widget_data_t *)w->data)->skin;

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
    char *d = ((widget_data_t *)w->data)->action_data;

    if(d) {
	XTK_SLIDER(w, s);
	if(s) {
	    double *pos = malloc(sizeof(*pos));
	    *pos = *((double*)p);
	    change_variable(d, pos);
	}
    }

    return 0;
}

static int
set_text(xtk_widget_t *w, void *p)
{
    char *d = ((widget_data_t *)w->data)->action_data;

    if(d) {
	fprintf(stderr, "set_text(%s) not yet implemented\n", d);
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
    if(xw->data) {
	widget_data_t *wd = xw->data;
	show_widgets(wd->skin, strdup(wd->action_data), 1);
	return 0;
    }
    return -1;
}


static int
disable_widgets(xtk_widget_t *xw, void *p)
{
    if(xw->data) {
	widget_data_t *wd = xw->data;
	show_widgets(wd->skin, strdup(wd->action_data), 0);
	return 0;
    }
    return -1;
}



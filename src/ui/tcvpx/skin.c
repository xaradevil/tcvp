/**
    Copyright (C) 2003  Michael Ahlberg, M�ns Rullg�rd

    Licensed under the Open Software License version 2.0
**/

#include <tcconf.h>
#include <tcalloc.h>
#include "tcvpx.h"
#include <string.h>
#include "tcvpctl.h"
#include <X11/Xlib.h>

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
hash_table *action_hash;

typedef struct {
    char *name;
    char *fmt;
    union {
	int integer;
	char* string;
    } value;
} parameter_t;

extern image_info_t*
load_image(char *skinpath, char *file)
{
    if(file) {
	FILE *f;


	char fn[1024];
	image_info_t *img;

	snprintf(fn, 1023, "%s/%s", skinpath, file);
	f = fopen(fn,"r");
	if(f) {
	    img = malloc(sizeof(image_info_t));

	    img->flags = IMAGE_COLOR_TYPE | IMAGE_SWAP_ORDER;
	    img->color_type = IMAGE_COLOR_TYPE_RGB_ALPHA;
	    img->iodata = f;
	    img->iofn = (vfs_fread_t)fread;
	    image_png_read(img);
	    fclose(f);

	    return img;
	}
    }
    return NULL;
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

    action_hash = hash_new(10, 0);

    for(i=0; actions[i].name != NULL; i++) {
	hash_search(action_hash, actions[i].name, actions[i].action, NULL);
    }

    return 0;
}

extern void
cleanup_skins(void)
{
    hash_destroy(action_hash, NULL);
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

	    hash_find(action_hash, c, &acb);
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
    if(tmp == NULL) {
	free(skin->path);
	skin->path = strdup("");	
    } else {
	*tmp=0;
    }

/*     if(tcconf_getvalue(skin->config, "name", "%s", &tmp) == 1) */
/* 	printf("Loaded skin: \"%s\"\n", tmp); */

    i = tcconf_getvalue(skin->config, "size", "%d %d", &skin->width,
			&skin->height);
    if(i != 2) {
	free(skin);
	return NULL;
    }

    skin->id_hash = hash_new(10, 0);

    return skin;
}

static void
free_skin(skin_t *skin)
{
    free(skin->path);
    tcfree(skin->config);
    hash_destroy(skin->id_hash, NULL);
    free(skin);
}

static xtk_widget_t*
create_skinned_background(window_t *win, skin_t *skin, tcconf_section_t *sec,
			  hash_table *parameters)
{
    char *file;
    int i=0;
    xtk_widget_t *w;

    i += tcconf_getvalue(sec, "background", "%s", &file);

    if(i != 1){
	return NULL;
    }

    w = xtk_create_background(win, load_image(skin->path, file));
    free(file);
    return w;
}


static int
destroy_skinned_box(xtk_widget_t *w)
{
    free(w->data);
    return 0;
}

static xtk_widget_t*
create_skinned_box(window_t *win, skin_t *skin, tcconf_section_t *sec,
		   hash_table *parameters)
{
    int x, y, width, height;
    widget_data_t *wd = calloc(sizeof(*wd), 1);
    int i=0;

    i += tcconf_getvalue(sec, "position", "%d %d", &x, &y);
    i += tcconf_getvalue(sec, "size", "%d %d", &width, &height);

    if(i != 4){
	return NULL;
    }

    xtk_widget_t *w = xtk_create_box(win, x, y, width, height, wd);
    w->ondestroy = destroy_skinned_box;

    create_ui(xtk_box_get_subwindow(w), skin, sec, parameters);

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
create_skinned_button(window_t *win, skin_t *skin, tcconf_section_t *sec,
		      hash_table *parameters)
{
    char *file, *of = NULL, *df = NULL, *bg = NULL;
    int x, y;
    int i=0;
    widget_data_t *wd = calloc(sizeof(*wd), 1);
    xtk_widget_t *bt;

    i += tcconf_getvalue(sec, "action", "%s", &wd->action);
    i += tcconf_getvalue(sec, "image", "%s", &file);
    i += tcconf_getvalue(sec, "position", "%d %d", &x, &y);

    if(i != 4){
	return NULL;
    }

    tcconf_getvalue(sec, "mouse_over", "%s", &of);
    tcconf_getvalue(sec, "pressed", "%s", &df);
    wd->skin = skin;
    tcconf_getvalue(sec, "background", "%s", &bg);

    bt = xtk_create_button(win, x, y, load_image(skin->path, bg),
			   load_image(skin->path, file),
			   load_image(skin->path, of),
			   load_image(skin->path, df), lookup_action, wd);
    bt->ondestroy = destroy_skinned_button;

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
create_skinned_label(window_t *win, skin_t *skin, tcconf_section_t *sec,
		     hash_table *parameters)
{
    int x, y;
    int width, height;
    int xoff = 0, yoff = 0;
    char *font;
    char *color, *align_s;
    int alpha;
    int stype, align;
    int i=0, j;
    char *action = NULL, *bg = NULL, *text, *default_text;
    widget_data_t *wd = calloc(sizeof(*wd), 1);
    xtk_widget_t *l;

    i += tcconf_getvalue(sec, "position", "%d %d", &x, &y);
    i += tcconf_getvalue(sec, "size", "%d %d", &width, &height);

    i += tcconf_getvalue(sec, "text", "%s", &text);
/*     i += tcconf_getvalue(sec, "text_offset", "%d %d", &xoff, &yoff); */
    i += tcconf_getvalue(sec, "font", "%s", &font);
    if((j = tcconf_getvalue(sec, "color", "%s %d", &color, &alpha))==1){
	alpha = 0xff;
	j++;
    }
    i += j;
    if((j = tcconf_getvalue(sec, "scroll_style", "%d", &stype))<1){
	stype = 1;
	j = 1;
    }
    i += j;
    if((j = tcconf_getvalue(sec, "align", "%s", &align_s))<1){
	j = 1;
	align_s = strdup("left");
    }
    i += j;
    if(strcasecmp(align_s, "left") == 0) {
	align = TCLABELLEFT;
    } else if(strcasecmp(align_s, "right") == 0) {
	align = TCLABELRIGHT;
    } else {
	align = TCLABELCENTER;
    }
    free(align_s);

    if(i != 10){
	return NULL;
    }

    tcconf_getvalue(sec, "action", "%s", &action);
    tcconf_getvalue(sec, "background", "%s", &bg);

    default_text = malloc(1024);
    parse_text(text, default_text);

    wd->action = action;
    wd->value = text;
    wd->skin = skin;
    l = xtk_create_label(win, x, y, width, height, xoff, yoff,
			 load_image(skin->path, bg), default_text,
			 font, color, alpha, stype, align,
			 lookup_action, wd);

    register_textwidget(l, text);
    l->ondestroy = destroy_skinned_label;
    free(default_text);
    free(color);
    free(font);
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
create_skinned_seek_bar(window_t *win, skin_t *skin, tcconf_section_t *sec,
			hash_table *parameters)
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

    s = xtk_create_seek_bar(win, x, y, sp_x, sp_y, ep_x, ep_y,
			    load_image(skin->path, bg),
			    load_image(skin->path, indicator),
			    load_image(skin->path, ind_over),
			    load_image(skin->path, ind_down),
			    *position, lookup_action, wd);
    if(s) {
	register_varwidget((xtk_widget_t *)s, wd->value);
	s->ondestroy = destroy_skinned_seek_bar;
	if(disable) xtk_disable_seek_bar(s);
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
create_skinned_state(window_t *win, skin_t *skin, tcconf_section_t *sec,
		     hash_table *parameters)
{
    int x, y;
    int ns = 0;
    image_info_t **imgs = NULL;
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
    tcconf_getvalue(sec, "background", "%s", &bg);

    parse_text(wd->value, def_state);

    if(ns > 0) {
	s = xtk_create_state(win, x, y, load_image(skin->path, bg),
			     ns, imgs, states, def_state, lookup_action, wd);
	register_textwidget((xtk_widget_t *)s, wd->value);
	s->ondestroy = destroy_skinned_state;
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
	xtk_position_t *pos = xtk_get_window_position(os->window);
	if(pos) {
	    xtk_size_t *ss = xtk_get_screen_size();
	    if(pos->x + s->width > ss->w || pos->x + os->width == ss->w) {
		pos->x = ss->w - s->width;
	    }
	    if(pos->y + s->height > ss->h || pos->y + os->height == ss->h) {
		pos->y = ss->h - s->height;
	    }
	    xtk_set_window_position(s->window, pos);
	    free(pos);
	    free(ss);
	}
	tcvp_close_ui(w, p);
    }
    return 0;
}

static int
tcvp_close_ui(xtk_widget_t *w, void *p)
{
    skin_t *s = ((widget_data_t *)w->data)->skin;

    xtk_destroy_window(w->window);
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

    skin->window = xtk_create_window("TCVP", skin->width, skin->height);

    create_ui(skin->window, skin, skin->config, NULL);

    xtk_show_window(skin->window);

    if((s->state & ST_STICKY) != 0) {
	xtk_set_sticky(skin->window, 1);
	skin->state |= ST_STICKY;
    }

    if((s->state & ST_ON_TOP) != 0) {
	xtk_set_always_on_top(skin->window, 1);
	skin->state |= ST_ON_TOP;
    }

    xtk_repaint_widgets();
    xtk_draw_widgets();

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
	w = fn(win, skin, sec, parameters);				\
	if(!w) {							\
	    fprintf(stderr, "Widget \"%s\", could not be created.\n",	\
		    name);						\
	} else {							\
	    tcconf_getvalue(sec, "id", "%s", &id);			\
	    if(id){							\
		hash_replace(skin->id_hash, id, w);			\
		free(id);						\
	    }								\
	    tcconf_getvalue(sec, "enabled", "%d", &e);			\
	    if(e != 0) xtk_show_widget(w);				\
	}								\
	tcfree(sec);							\
    }									\
} while(0);

extern int
create_ui(window_t *win, skin_t *skin, tcconf_section_t *config,
	  hash_table *parameters)
{
    void *w;

    w = create_skinned_background(win, skin, config, parameters);
    if(w) xtk_show_widget(w);

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
	    xtk_set_sticky(s->window, 0);
	    s->state &= ~ST_STICKY;	
	} else {
	    xtk_set_sticky(s->window, 1);
	    s->state |= ST_STICKY;
	}
    } else if(strcasecmp(d, "set") == 0) {
	xtk_set_sticky(s->window, 1);
	s->state |= ST_STICKY;	
    } else if(strcasecmp(d, "unset") == 0) {
	xtk_set_sticky(s->window, 0);
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
	    xtk_set_always_on_top(s->window, 0);
	    s->state &= ~ST_ON_TOP;	
	} else {
	    xtk_set_always_on_top(s->window, 1);
	    s->state |= ST_ON_TOP;
	}
    } else if(strcasecmp(d, "set") == 0) {
	xtk_set_always_on_top(s->window, 1);
	s->state |= ST_ON_TOP;	
    } else if(strcasecmp(d, "unset") == 0) {
	xtk_set_always_on_top(s->window, 0);
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
	if(w->type == TCSEEKBAR) {
	    double *pos = malloc(sizeof(*pos));
	    *pos = *((double*)p);
	    change_variable(d, pos);
	} else {
	    fprintf(stderr, "set_variable(%s) not yet implemented for "
		    "widget type %d.\n", d, w->type);
	}
    }

    return 0;
}

static int
set_text(xtk_widget_t *w, void *p)
{
    char *d = ((widget_data_t *)w->data)->action_data;

    if(d) {
	fprintf(stderr, "set_text(%s) not yet implemented for "
		"widget type %d.\n", d, w->type);
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
	
	hash_find(skin->id_hash, tmp, &w);
	if(w) {
	    if(show) {
		xtk_show_widget(w);
	    } else {
		xtk_hide_widget(w);
	    }
	}
	tmp = next;
    }

    free(d);

    xtk_repaint_widgets();
    xtk_draw_widgets();

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



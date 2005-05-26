/**
    Copyright (C) 2005  Michael Ahlberg, Måns Rullgård

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

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <xmms/xmmsctrl.h>
#include <xmms/configfile.h>
#include <xmms/fullscreen.h>
#include <xmms/util.h>
#include <tc2.h>

static void *libxmms;

#define lxwrap(ret, name, args, argnames)	\
static ret (*dl_##name) args;			\
extern ret					\
name args					\
{						\
    if(!dl_##name)				\
	dl_##name = dlsym(libxmms, #name);	\
    return dl_##name argnames;			\
}

lxwrap(ConfigFile *, xmms_cfg_new, (void), ())
lxwrap(ConfigFile *, xmms_cfg_open_file, (gchar * filename), (filename))
lxwrap(gboolean, xmms_cfg_write_file, (ConfigFile * cfg, gchar * filename),
       (cfg, filename))
lxwrap(void, xmms_cfg_free, (ConfigFile * cfg), (cfg))
lxwrap(ConfigFile *, xmms_cfg_open_default_file, (void), ())
lxwrap(gboolean, xmms_cfg_write_default_file, (ConfigFile * cfg), (cfg))

lxwrap(gboolean, xmms_cfg_read_string,
       (ConfigFile * cfg, gchar * section, gchar * key, gchar ** value),
       (cfg, section, key, value))
lxwrap(gboolean, xmms_cfg_read_int,
       (ConfigFile * cfg, gchar * section, gchar * key, gint * value),
       (cfg, section, key, value))
lxwrap(gboolean, xmms_cfg_read_boolean,
       (ConfigFile * cfg, gchar * section, gchar * key, gboolean * value),
       (cfg, section, key, value))
lxwrap(gboolean, xmms_cfg_read_float,
       (ConfigFile * cfg, gchar * section, gchar * key, gfloat * value),
       (cfg, section, key, value))
lxwrap(gboolean, xmms_cfg_read_double,
       (ConfigFile * cfg, gchar * section, gchar * key, gdouble * value),
       (cfg, section, key, value))

lxwrap(void, xmms_cfg_write_string,
       (ConfigFile * cfg, gchar * section, gchar * key, gchar * value),
       (cfg, section, key, value))
lxwrap(void, xmms_cfg_write_int,
       (ConfigFile * cfg, gchar * section, gchar * key, gint value),
       (cfg, section, key, value))
lxwrap(void, xmms_cfg_write_boolean,
       (ConfigFile * cfg, gchar * section, gchar * key, gboolean value),
       (cfg, section, key, value))
lxwrap(void, xmms_cfg_write_float,
       (ConfigFile * cfg, gchar * section, gchar * key, gfloat value),
       (cfg, section, key, value))
lxwrap(void, xmms_cfg_write_double,
       (ConfigFile * cfg, gchar * section, gchar * key, gdouble value),
       (cfg, section, key, value))

lxwrap(void, xmms_cfg_remove_key,
       (ConfigFile * cfg, gchar * section, gchar * key),
       (cfg, section, key))

extern gint
xmms_remote_get_main_volume(gint session)
{
    return 0;
}

extern void
xmms_remote_set_main_volume(gint session, gint v)
{
}

extern gint
xmms_remote_get_playlist_pos(gint session)
{
    return 0;
}

extern gchar *
xmms_remote_get_playlist_title(gint session, gint pos)
{
    return "";
}

extern void
xmms_remote_playlist_prev(gint session)
{
}

extern void
xmms_remote_playlist_next(gint session)
{
}

extern void
xmms_remote_play(gint session)
{
}

extern void
xmms_remote_pause(gint session)
{
}

extern void
xmms_remote_stop(gint session)
{
}

extern gint
xmms_remote_get_output_time(gint session)
{
    return 0;
}

extern void
xmms_remote_jump_to_time(gint session, gint pos)
{
}

extern int
xmms_check_realtime_priority(void)
{
    return 0;
}

extern gboolean
xmms_fullscreen_available(Display *dpy){
    return 0;
}

extern gboolean
xmms_fullscreen_init(GtkWidget *win)
{
    return 0;
}

extern gboolean
xmms_fullscreen_enter(GtkWidget *win, gint *w, gint *h)
{
    return 0;
}

extern void
xmms_fullscreen_leave(GtkWidget *win)
{
}

extern gboolean
xmms_fullscreen_in(GtkWidget *win)
{
    return 0;
}

extern gboolean
xmms_fullscreen_mark(GtkWidget *win)
{
    return 0;
}

extern void
xmms_fullscreen_unmark(GtkWidget *win)
{
}

extern void
xmms_fullscreen_cleanup(GtkWidget *win)
{
}

extern int
libxmms_init(void)
{
    libxmms = dlopen("libxmms.so", RTLD_NOW);
    if(!libxmms){
	tc2_print("XMMS", TC2_PRINT_ERROR, "%s\n", dlerror());
	return -1;
    }

    return 0;
}

extern void
libxmms_cleanup(void)
{
    dlclose(libxmms);
}

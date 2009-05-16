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
#include <unistd.h>
#include <pthread.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <gtk/gtk.h>
#include <xmms_tc2.h>

static void *libxmms;
static pthread_t gtkth;

static void *
run_gtk(void *p)
{
    GDK_THREADS_ENTER();
    gtk_main();
    GDK_THREADS_LEAVE();

    return NULL;
}

extern int
xmms_init(char *p)
{
    int argc = 1;
    char *argv0 = "tcvp", **argv = &argv0;
    int (*init)(void);

    tc2_print("XMMS", TC2_PRINT_DEBUG, "starting GTK\n");
    g_thread_init(NULL);
    if(gtk_init_check(&argc, &argv)){
        gdk_rgb_init();
        gtk_widget_set_default_colormap(gdk_rgb_get_cmap());
        gtk_widget_set_default_visual(gdk_rgb_get_visual());
        pthread_create(&gtkth, NULL, run_gtk, NULL);
    }

    libxmms = dlopen(LIBDIR "/libxmms.so", RTLD_GLOBAL | RTLD_NOW);
    if(!libxmms){
        tc2_print("XMMS", TC2_PRINT_ERROR, "%s\n", dlerror());
        return -1;
    }

    init = dlsym(libxmms, "libxmms_init");
    if(!init || init()){
        return -1;
    }

    return 0;
}

static int
stop_gtk(void *p)
{
    GDK_THREADS_ENTER();
    gtk_main_quit();
    GDK_THREADS_LEAVE();
    return 0;
}

extern int
xmms_shutdown(void)
{
    if(gtkth){
        tc2_print("XMMS", TC2_PRINT_DEBUG, "stopping GTK\n");
        GDK_THREADS_ENTER();
        gtk_timeout_add(10, stop_gtk, NULL);
        GDK_THREADS_LEAVE();
        pthread_join(gtkth, NULL);
    }

    dlclose(libxmms);
    return 0;
}

/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

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

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <gtk/gtk.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tcvp_types.h>
#include <tcvpgtk_tc2.h>

static player_t *player;
static char *last_dir=NULL;
static pthread_t gui_thread;

static void
tcvp_pause(GtkWidget *widget, gpointer callback_data)
{
    static int paused = 0;

    if(!player)
	return;

    (paused ^= 1)? player->stop(player): player->start(player);
}

static void
tcvp_stop(GtkWidget *widget, gpointer callback_data)
{
    if(player){
	player->close(player);
	player = NULL;
    }
}

static int
tcvp_play(char *file)
{
    tcvp_stop(NULL, NULL);

    if(!(player = tcvp_open(file, NULL, NULL, NULL)))
	return -1;

    player->start(player);

    return 0;
}

void file_ok_sel(GtkWidget *w, GtkFileSelection *fs)
{
    char *p;
    if(last_dir!=NULL)
	free(last_dir);
    last_dir=strdup((char *)gtk_file_selection_get_filename(GTK_FILE_SELECTION(fs)));
    p=strrchr(last_dir, '/');
    p[1]=0;
    tcvp_play((char *)gtk_file_selection_get_filename(GTK_FILE_SELECTION(fs)));
    gtk_widget_destroy(GTK_WIDGET(fs));
}

void open_file(GtkWidget *widget, gpointer callback_data)
{
    GtkWidget *filew;
    char *dir=last_dir?last_dir:"/mp3/";

    filew = gtk_file_selection_new ("File selection");
    gtk_file_selection_set_filename(GTK_FILE_SELECTION(filew), dir);
    g_signal_connect(G_OBJECT(GTK_FILE_SELECTION(filew)->ok_button),
                      "clicked", G_CALLBACK(file_ok_sel), (gpointer)filew);
    
    g_signal_connect_swapped(G_OBJECT(GTK_FILE_SELECTION(filew)->cancel_button),
                              "clicked", G_CALLBACK(gtk_widget_destroy),
                              G_OBJECT (filew));
    

    gtk_widget_show(filew);
}

void gui_quit(GtkWidget *widget, gpointer callback_data)
{
    tc2_request(TC2_UNLOAD_MODULE, 0, "TCVP/gtkui");
}

void gui_init()
{
    GtkWidget *window;
    GtkWidget *sbt, *pbt, *psb;
    GtkWidget *box1;

    gtk_init(0, NULL);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "TCVP");
    gtk_signal_connect(GTK_OBJECT(window), "delete_event",
		       GTK_SIGNAL_FUNC (gui_quit), NULL);

    gtk_container_set_border_width(GTK_CONTAINER(window), 5);

    box1 = gtk_hbox_new (FALSE, 0);

    sbt = gtk_button_new_with_label("Stop");
    pbt = gtk_button_new_with_label("Play");
    psb = gtk_button_new_with_label("Pause");

    g_signal_connect(G_OBJECT(sbt), "clicked",
		     G_CALLBACK(tcvp_stop), NULL);
    g_signal_connect(G_OBJECT(pbt), "clicked",
		     G_CALLBACK(open_file), NULL);
    g_signal_connect(G_OBJECT(psb), "clicked",
		     G_CALLBACK(tcvp_pause), NULL);

    gtk_container_add(GTK_CONTAINER(window), box1);

    gtk_box_pack_start(GTK_BOX(box1), pbt, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box1), psb, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box1), sbt, TRUE, TRUE, 0);

    gtk_widget_show(sbt);
    gtk_widget_show(pbt);
    gtk_widget_show(psb);

    gtk_widget_show(box1);

    gtk_widget_show(window);
}

void *gui_th(void *p)
{
    gtk_main();

    pthread_exit(NULL);
}

extern int
tcvpgtk_init(char *p)
{
    gui_init();
    pthread_create(&gui_thread, NULL, gui_th, NULL);

    return 0;
}

extern int
tcvpgtk_shdn(void)
{
    tcvp_stop(NULL, NULL);

    gtk_main_quit();

    pthread_join(gui_thread, NULL);

    return 0;
}

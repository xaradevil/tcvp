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

static int
tcvp_pause(char *p)
{
    static int paused = 0;

    if(!player)
	return -1;

    (paused ^= 1)? player->stop(player): player->start(player);

    return 0;
}

static int
tcvp_stop(char *p)
{
    if(player){
	player->close(player);
	player = NULL;
    }

    return 0;
}

static int
tcvp_play(char *file)
{
    tcvp_stop(NULL);

    if(!(player = tcvp_open(file)))
	return -1;

    player->start(player);

    return 0;
}

void stop_file(GtkWidget *widget, gpointer callback_data)
{
    tcvp_stop("");
}

void file_ok_sel(GtkWidget *w, GtkFileSelection *fs )
{
    char *p;
    if(last_dir!=NULL) free(last_dir);
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
    char *c = strdup("TCVP/gtkui");
    tc2_request(TC2_UNLOAD_MODULE, 0, c);
}

void gui_init()
{
    GtkWidget *window;
    GtkWidget *sbt, *pbt;
    GtkWidget *box1;

    gtk_init(0, NULL);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_signal_connect(GTK_OBJECT(window), "delete_event",
		       GTK_SIGNAL_FUNC (gui_quit), NULL);

    gtk_container_set_border_width(GTK_CONTAINER(window), 5);

    box1 = gtk_hbox_new (FALSE, 0);

    sbt=gtk_button_new_with_label("Stop");
    pbt=gtk_button_new_with_label("Play");

    g_signal_connect (G_OBJECT(sbt), "clicked",
                      G_CALLBACK(stop_file), player);
    g_signal_connect (G_OBJECT(pbt), "clicked",
                      G_CALLBACK(open_file), NULL);

    gtk_container_add(GTK_CONTAINER(window), box1);

    gtk_box_pack_start(GTK_BOX(box1), pbt, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box1), sbt, TRUE, TRUE, 0);

    gtk_widget_show(sbt);
    gtk_widget_show(pbt);

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
    tcvp_stop(NULL);

    gtk_main_quit();

    pthread_join(gui_thread, NULL);

    return 0;
}

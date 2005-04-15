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
#include "tcvpctl.h"
#include <string.h>
#include <unistd.h>

static int tcvpstate=-1;
int64_t s_pos, s_length, start_time;
static int show_time = TCTIME_ELAPSED;
static muxed_stream_t *st = NULL;


extern int
tcvpx_event(tcvp_module_t *tm, tcvp_event_t *te)
{
    if(te->type == TCVP_STATE) {
	tcvpstate = ((tcvp_state_event_t *)te)->state;

	switch(tcvpstate) {
	case TCVP_STATE_PLAYING:
	    change_text("state", "play");
	    break;

	case TCVP_STATE_STOPPED:
	    change_text("state", "pause");
	    break;

	case TCVP_STATE_END:
	    change_text("state", "stop");
	    s_pos = -1;
	    update_time();
	    if(st)
		tcfree(st);
	    st = NULL;
	    break;
	}
    } else if(te->type == TCVP_PL_STATE){
	tcvp_pl_state_event_t *pls = (tcvp_pl_state_event_t *) te;

	if(pls->state == TCVP_PL_STATE_END){
	    tcfree(st);
	    st = NULL;
	    tcvp_stop(NULL, NULL);
	} else {
	    int32_t *plc = tcalloc(sizeof(*plc));
	    *plc = pls->current;
	    change_variable("playlist_current_position", "integer", plc);
	}
    } else if(te->type == TCVP_TIMER) {
	s_pos = ((tcvp_timer_event_t *)te)->time;
	if(s_pos-start_time > s_length)
	    s_length = s_pos-start_time;
	update_time();

    } else if(te->type == TCVP_LOAD || te->type == TCVP_STREAM_INFO) {
	if(te->type == TCVP_LOAD) {
	    char *title;

	    if(st) tcfree(st);
	    st = ((tcvp_load_event_t *)te)->stream;
	    tcref(st);

	    s_length = 0;

	    if((title = tcattr_get(st, "title"))){
		change_text("title", title);
	    } else {
		char *ext;
		char *file = tcattr_get(st, "file");

		if(file){
		    title = strrchr(file, '/');
		    title = strdup(title? title + 1: file);
		    ext = strrchr(title, '.');
		    if(ext)
			*ext = 0;

		    change_text("title", title);
		    free(title);
		} else {
		    change_text("title", NULL);
		}
	    }

	    change_text("performer", tcattr_get(st, "performer"));
	    change_text("album", tcattr_get(st, "album"));
	    change_text("genre", tcattr_get(st, "genre"));
	    change_text("year", tcattr_get(st, "year"));
	    change_text("trackno", tcattr_get(st, "track"));
	}

	if(st) {
	    int i;

	    if(st->n_streams > 0) {
		start_time = st->streams[0].common.start_time;
	    }

	    for(i = 0; i < st->n_streams; i++) {
		if(st->used_streams[i]) {
		    stream_t *s = &st->streams[i];
		    if(s->stream_type == STREAM_TYPE_VIDEO) {
			char buf[10];
			sprintf(buf, "%.3f",
				(double)s->video.frame_rate.num /
				s->video.frame_rate.den);
			change_text("video_framerate", buf);
		    } else if(s->stream_type == STREAM_TYPE_AUDIO) {
			char buf[10];
			sprintf(buf, "%d", s->audio.bit_rate/1000);
			change_text("audio_bitrate", buf);
			    
			sprintf(buf, "%.1f",
				(double)s->audio.sample_rate/1000);
			change_text("audio_samplerate", buf);

			sprintf(buf, "%d", s->audio.channels);
			change_text("audio_channels", buf);
		    }
		}
	    }
	    if(st->time)
		s_length = st->time;

	    update_time();
	}
    } else if(te->type == TCVP_PL_CONTENT){
	int i;
	int *length = tcalloc(sizeof(*length));

	tcvp_pl_content_event_t *plce = (tcvp_pl_content_event_t *)te;

	char **entries = tcalloc((plce->length+1) * sizeof(*entries));

	for(i=0; i<plce->length; i++) {
	    entries[i] = strdup(plce->names[i]);
	}
	entries[i] = NULL;

	*length = plce->length;
	change_variable("playlist_number_of_entries", "integer", length);

	change_variable("playlist_entries", "string_array", entries);
    }

    return 0;
}


extern int
tcvp_pause(xtk_widget_t *w, void *p)
{
    tcvp_event_send(qs, TCVP_PAUSE);
    return 0;
}


extern int
tcvp_stop(xtk_widget_t *w, void *p)
{
    tcvp_event_send(qs, TCVP_PL_STOP);
    tcvp_event_send(qs, TCVP_CLOSE);

    return 0;
}


extern int
tcvp_play(xtk_widget_t *w, void *p)
{
    if(tcvpstate == TCVP_STATE_STOPPED) {
	tcvp_pause(w, p);
    } else {
	if(tcvpstate != TCVP_STATE_PLAYING) {
	    tcvp_event_send(qs, TCVP_PL_START);
	} else {
	    tcvp_stop(w, p);
	    tcvp_event_send(qs, TCVP_PL_START);
	}
    }

    return 0;
}


extern int
tcvp_next(xtk_widget_t *w, void *p)
{
    tcvp_event_send(qs, TCVP_PL_NEXT);

    return 0;
}


extern int
tcvp_previous(xtk_widget_t *w, void *p)
{
    tcvp_event_send(qs, TCVP_PL_PREV);

    return 0;
}


extern int
tcvp_seek(xtk_widget_t *w, void *p)
{
    double pos = *((double*)p);
    uint64_t time = s_length * pos + start_time;

    tcvp_event_send(qs, TCVP_SEEK, time / 27000, TCVP_SEEK_ABS);

    return 0;
}


extern int
tcvp_seek_rel(xtk_widget_t *w, void *p)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    if(wd) {
	char *d = wd->action_data;
	uint64_t time = strtol(d, NULL, 0);

	tcvp_event_send(qs, TCVP_SEEK, time * 1000, TCVP_SEEK_REL);
    }

    return 0;
}


extern int
tcvp_quit(void)
{
    tc2_request(TC2_UNLOAD_ALL, 0);

    return 0;
}


extern int
tcvp_playlist_query(xtk_widget_t *w, void *p)
{
    tcvp_event_send(qs, TCVP_PL_QUERY);

    return 0;
}


extern int
tcvp_add_file(char *file)
{
/*     fprintf(stderr, "%s\n", file); */
    char *s = strrchr(file, '.');
    if(s && !strcasecmp(s, ".m3u")){
	tcvp_event_send(qs, TCVP_PL_ADDLIST, file, -1);	
    } else {
	tcvp_event_send(qs, TCVP_PL_ADD, &file, 1, -1);
    }

    return 0;
}


extern int
toggle_time(xtk_widget_t *w, void *p)
{
    if(show_time == TCTIME_ELAPSED) {
	show_time = TCTIME_REMAINING;
    } else if(show_time == TCTIME_REMAINING) {
	show_time = TCTIME_ELAPSED;
    }
    update_time();

    return 0;
}


extern int
update_time(void)
{
    char text[8];
    int t = 0;
    char sign = ' ';
    double *pos = tcalloc(sizeof(*pos));

    if(show_time == TCTIME_ELAPSED) {
	if(tcvp_ui_tcvpx_conf_time_offset) {
	    t = (s_pos - start_time) / 27000000;
	} else {
	    t = s_pos / 27000000;
	}
    } else if(show_time == TCTIME_REMAINING) {
	sign = '-';
	if(s_length > 0){
	    t = (s_length - (s_pos - start_time)) / 27000000;
	} else {
	    t = 0;
	}
    }

    if(s_pos >= 0 && s_length > 0 && s_pos - start_time>=0) {
	*pos = ((double)(s_pos - start_time)/27000000)/(s_length/27000000);
    } else {
	*pos = -1;
    }
    change_variable("position", "double", pos);

    int m = t/60;

    if(s_pos >= 0 && s_pos - start_time >= 0) {
	snprintf(text, 8, "%c%d:%02d", sign, m, t%60);
	change_text("time", text);
    } else {
	change_text("time", "   :  ");
    }

    return 0;
}


extern int
tcvp_playlist_remove(xtk_widget_t *w, void *p)
{
    widget_data_t *wd = xtk_widget_get_data(w);
    char *d = wd->action_data;
    char *next;
    int pos = 0;
    int num = 1;

    pos = strtol(d, &next, 10);
    if(next) num = strtol(next, NULL, 10);

    tcvp_event_send(qs, TCVP_PL_REMOVE, pos, num);

    return 0;
}

extern void
free_ctl(void)
{
    tcfree(st);
}

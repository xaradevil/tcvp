/**
    Copyright (C) 2006  Michael Ahlberg, Måns Rullgård

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
#include <unistd.h>
#include <tcstring.h>
#include <tctypes.h>
#include <tclist.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <math.h>
#include <equalizer_tc2.h>

#define EQ_BANDS 10
#define EQ_CHANNELS 16
#define EQ_IN_FACTOR 0.25


typedef struct equalizer {
    int eq_on;

    char *frequency[EQ_BANDS];
    float alpha[EQ_BANDS];
    float beta[EQ_BANDS];
    float gamma[EQ_BANDS];

    float amp[EQ_BANDS];
    float preamp;

    float ampdb[EQ_BANDS];
    float preampdb;

    float x[EQ_CHANNELS][2];
    float y[EQ_CHANNELS][EQ_BANDS][2];
} equalizer_t;


static int eq_save(equalizer_t *eq);
static tcconf_section_t *current = NULL;


/* Algorithm & coefficients from vlc */
typedef struct
{
    struct {
        char *frequency;
        float alpha;
        float beta;
        float gamma;
    } band[EQ_BANDS];

} eq_config_t;


static const eq_config_t eq_config_44100 =
{
    { {  "60", 0.003013, 0.993973, 1.993901 },
      { "170", 0.008490, 0.983019, 1.982437 },
      { "310", 0.015374, 0.969252, 1.967331 },
      { "600", 0.029328, 0.941343, 1.934254 },
      {  "1k", 0.047918, 0.904163, 1.884869 },
      {  "3k", 0.130408, 0.739184, 1.582718 },
      {  "6k", 0.226555, 0.546889, 1.015267 },
      { "12k", 0.344937, 0.310127, -0.181410 },
      { "14k", 0.366438, 0.267123, -0.521151 },
      { "16k", 0.379009, 0.241981, -0.808451 } }
};
static const eq_config_t eq_config_48000 =
{
    { {  "60", 0.002769, 0.994462, 1.994400 },
      { "170", 0.007806, 0.984388, 1.983897 },
      { "310", 0.014143, 0.971714, 1.970091 },
      { "600", 0.027011, 0.945978, 1.939979 },
      {  "1k", 0.044203, 0.911595, 1.895241 },
      {  "3k", 0.121223, 0.757553, 1.623767 },
      {  "6k", 0.212888, 0.574224, 1.113145 },
      { "12k", 0.331347, 0.337307, 0.000000 },
      { "14k", 0.355263, 0.289473, -0.333740 },
      { "16k", 0.371900, 0.256201, -0.628100 } }
};



extern int
eq_input(tcvp_pipe_t *p, tcvp_data_packet_t *pk)
{
    equalizer_t *eq = p->private;

    if(pk->data && eq->eq_on){
	int nch = p->format.audio.channels;
	int samples = pk->sizes[0]/2;
	int ch, i, j;
	int16_t *ptr = (int16_t*) pk->data[0];


	for(i = 0; i < samples; i+=nch) {
	    for(ch = 0; ch < nch; ch++) {
		const int16_t x = ptr[ch];
		float o = 0.0;

		for(j = 0; j < EQ_BANDS; j++) {
		    float y =
			eq->alpha[j] * (x - eq->x[ch][1]) -
			eq->beta[j]  * eq->y[ch][j][1] +
			eq->gamma[j] * eq->y[ch][j][0];

		    eq->y[ch][j][1] = eq->y[ch][j][0];
		    eq->y[ch][j][0] = y;

		    o += y * eq->amp[j];
		}
		eq->x[ch][1] = eq->x[ch][0];
		eq->x[ch][0] = x;
		ptr[ch] = (int16_t) (eq->preamp * (EQ_IN_FACTOR * x + o));
	    }
	    ptr += nch;
	}
    }

    return p->next->input(p->next, (tcvp_packet_t *) pk);
}


extern int
eq_probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
{ 
    int i;
    eq_config_t eqc;
    equalizer_t *eq = p->private;

    if(p->format.audio.channels > EQ_CHANNELS) {
	tc2_print("EQUALIZER", TC2_PRINT_ERROR, "The equalizer support a maximum of %d channels\n", EQ_CHANNELS);
	return PROBE_FAIL;
    }	

    if(p->format.audio.sample_rate == 48000) {
	eqc = eq_config_48000;
    } else if(p->format.audio.sample_rate == 44100) {
	eqc = eq_config_44100;	
    } else {
	tc2_print("EQUALIZER", TC2_PRINT_ERROR, "The equalizer only support samplerates of 48000 Hz and 44100 Hz\n");
	return PROBE_FAIL;
    }

    for(i = 0; i < EQ_BANDS; i++) {
	eq->frequency[i] = eqc.band[i].frequency;
	eq->alpha[i] = eqc.band[i].alpha;	
	eq->beta[i]  = eqc.band[i].beta;	
	eq->gamma[i] = eqc.band[i].gamma;
    }

    return PROBE_OK;
}


static float convertdB(float db)
{
    if(db < -20.0) {
        db = -20.0;
    } else if( db > 20.0) {
        db = 20.0;
    }
    return pow(10, db/20.0);
}


static float fconvertdB(float db)
{
    return EQ_IN_FACTOR * (convertdB(db) - 1.0);
}


static int read_preset(tcconf_section_t *prsec, equalizer_t *eq)
{
    int n, i;
    float db = 0.0;

    tcconf_getvalue(prsec, "preamp", "%f", &db);
    eq->preamp = convertdB(db);
    eq->preampdb = db;
    tc2_print("EQUALIZER", TC2_PRINT_DEBUG+1, "Preamp %f dB\n", db);

    n = tcconf_getvalue(prsec, "values",
			"%f %f %f %f %f %f %f %f %f %f ",
			&eq->ampdb[0], &eq->ampdb[1],
			&eq->ampdb[2], &eq->ampdb[3],
			&eq->ampdb[4], &eq->ampdb[5],
			&eq->ampdb[6], &eq->ampdb[7],
			&eq->ampdb[8], &eq->ampdb[9]);

    for(i = 0; i < EQ_BANDS; i++) {
	tc2_print("EQUALIZER", TC2_PRINT_DEBUG+1, "Band %d %f dB\n",
		  i, eq->ampdb[i]);
	eq->amp[i] = fconvertdB(eq->ampdb[i]);
    }

    if(n < 10) {
	tc2_print("EQUALIZER", TC2_PRINT_ERROR, "Error in preset\n");
	return -1;
    }

    if(tcconf_getvalue(prsec, "eq_on", "%d", &n) == 1) {
	tc2_print("EQUALIZER", TC2_PRINT_DEBUG+1, "eq_on %d\n", n);
	eq->eq_on = n;
    }

    return 0;
}


extern int
eq_new(tcvp_pipe_t *p, stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
       muxed_stream_t *ms)
{
    equalizer_t *eq = tcallocz(sizeof(*eq));
    char *preset = NULL;

    char *prname = "TCVP/filter/equalizer";
    tcconf_section_t *prsec = tc2_get_conf(prname);
    tcconf_getvalue(cs, "preset", "%s", &preset);
    tcfree(prsec);

    if(current) {
	tc2_print("EQUALIZER", TC2_PRINT_DEBUG,
		  "Using previous equalizer setting\n");
	if(read_preset(current, eq) < 0) return -1;
    } else if(preset != NULL) {
	char prname[256];
	
	tc2_print("EQUALIZER", TC2_PRINT_DEBUG,
		  "Using equalizer preset '%s'\n", preset);
	snprintf(prname, 256, "TCVP/filter/equalizer/preset/%s", preset);
	if(!(prsec = tc2_get_conf(prname))){
	    tc2_print("EQUALIZER", TC2_PRINT_ERROR, "No preset '%s'\n",
		      preset);
	    return -1;
	}
	if(read_preset(prsec, eq) < 0) return -1;
	eq->eq_on = 1;
	tcfree(prsec);

	eq_save(eq);
    } else {
	int i;

	for(i = 0; i < EQ_BANDS; i++) {
	    eq->amp[i] = fconvertdB(0.0);
	}
	eq->preamp = convertdB(0.0);
	eq->eq_on = 1;

	eq_save(eq);
    }

    p->private = eq;
    free(preset);

    return 0;
}


extern int
eq_flush(tcvp_pipe_t *p, int drop)
{
    equalizer_t *eq = p->private;
    int ch, j;

    for(ch = 0; ch < EQ_CHANNELS; ch++) {
	eq->x[ch][0] = 0.0;
	eq->x[ch][1] = 0.0;
	for(j = 0; j < EQ_BANDS; j++) {
	    eq->y[ch][j][0] = 0.0;
	    eq->y[ch][j][1] = 0.0;
	}	
    }
    
    return 0;
}


extern int
eq_event(tcvp_module_t *p, tcvp_event_t *e)
{
    equalizer_t *eq = p->private;
    tcvp_eq_set_event_t *te = (tcvp_eq_set_event_t *) e;

    tc2_print("EQUALIZER", TC2_PRINT_DEBUG+1, "Event set %s to %d\n",
	      te->attribute, te->value);

    if(strcmp(te->attribute, "preamp") == 0) {
	eq->preamp = convertdB(te->value);
	eq->preampdb = te->value;
    } else {
	int i;
	for(i = 0; i < EQ_BANDS; i++) {
	    if(strcmp(te->attribute, eq->frequency[i]) == 0) {
		eq->ampdb[i] = te->value;
		eq->amp[i] = fconvertdB(eq->ampdb[i]);
		break;
	    }
	}
    }
    eq_save(eq);

    return 0;
}


static int
eq_save(equalizer_t *eq)
{
    if(current) tcfree(current);

    tc2_print("EQUALIZER", TC2_PRINT_DEBUG, "Saving preset\n");

    current = tcconf_new("current");
    
    tcconf_setvalue(current, "values", "%f %f %f %f %f %f %f %f %f %f",
		    eq->ampdb[0], eq->ampdb[1], 
		    eq->ampdb[2], eq->ampdb[3], 
		    eq->ampdb[4], eq->ampdb[5], 
		    eq->ampdb[6], eq->ampdb[7], 
		    eq->ampdb[8], eq->ampdb[9]);
    tcconf_setvalue(current, "preamp", "%f", eq->preampdb);
    tcconf_setvalue(current, "eq_on", "%d", eq->eq_on);

    return 0;
}


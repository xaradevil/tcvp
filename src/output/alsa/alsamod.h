/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#ifndef _ALSAMOD_H
#define _ALSAMOD_H

#define ALSA_PCM_NEW_HW_PARAMS_API 1
#include <alsa/asoundlib.h>
#include <alsa/pcm_plugin.h>

#include <tcvp_types.h>
#include <alsa_tc2.h>


#define RUN   1
#define STOP  2
#define PAUSE 3

extern timer_driver_t *open_timer(int res, snd_pcm_t *pcm);

#define PCM    0
#define SYSTEM 1

#endif

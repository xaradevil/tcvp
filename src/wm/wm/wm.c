/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <tcvp_types.h>
#include <wm_tc2.h>

extern window_manager_t *
wm_open(int w, int h, wm_update_t upd, void *cbd,
	tcconf_section_t *cs, int flags)
{
    return wm_x11_open(w, h, upd, cbd, cs, flags);
}

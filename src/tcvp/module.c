/**
    Copyright (C) 2003  Michael Ahlberg, Måns Rullgård

    Licensed under the Open Software License version 2.0
**/

#include <tcvp_mod.h>

extern int
tcvp_init(char *p)
{
    init_events();
    init_core();
    return 0;
}

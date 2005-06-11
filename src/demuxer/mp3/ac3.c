/**
    Copyright (C) 2003-2005  Michael Ahlberg, Måns Rullgård

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
#include <tcstring.h>
#include <tctypes.h>
#include <tcalloc.h>
#include <tcvp_types.h>
#include <mp3_tc2.h>
#include "mp3.h"

static int ac3_sample_rates[4] = {
    48000, 44100, 32000, 0
};

static int ac3_frame_sizes[64][3] = {
    { 64,   69,   96   },  
    { 64,   70,   96   },  
    { 80,   87,   120  },  
    { 80,   88,   120  },  
    { 96,   104,  144  },  
    { 96,   105,  144  },  
    { 112,  121,  168  }, 
    { 112,  122,  168  }, 
    { 128,  139,  192  }, 
    { 128,  140,  192  }, 
    { 160,  174,  240  }, 
    { 160,  175,  240  }, 
    { 192,  208,  288  }, 
    { 192,  209,  288  }, 
    { 224,  243,  336  }, 
    { 224,  244,  336  }, 
    { 256,  278,  384  }, 
    { 256,  279,  384  }, 
    { 320,  348,  480  }, 
    { 320,  349,  480  }, 
    { 384,  417,  576  }, 
    { 384,  418,  576  }, 
    { 448,  487,  672  }, 
    { 448,  488,  672  }, 
    { 512,  557,  768  }, 
    { 512,  558,  768  }, 
    { 640,  696,  960  }, 
    { 640,  697,  960  }, 
    { 768,  835,  1152 }, 
    { 768,  836,  1152 }, 
    { 896,  975,  1344 }, 
    { 896,  976,  1344 }, 
    { 1024, 1114, 1536 },
    { 1024, 1115, 1536 },
    { 1152, 1253, 1728 },
    { 1152, 1254, 1728 },
    { 1280, 1393, 1920 },
    { 1280, 1394, 1920 },
};

static int ac3_bitrates[64] = {
    32, 32, 40, 40, 48, 48, 56, 56, 64, 64, 80, 80, 96, 96, 112, 112,
    128, 128, 160, 160, 192, 192, 224, 224, 256, 256, 320, 320, 384,
    384, 448, 448, 512, 512, 576, 576, 640, 640,
};

extern int
ac3_header(u_char *head, mp3_frame_t *mf)
{
    int fscod, frmsizecod;

    if(head[0] != 0x0b || head[1] != 0x77)
	return -1;

    fscod = (head[4] >> 6) & 0x3;
    frmsizecod = head[4] & 0x3f;

    if(!ac3_sample_rates[fscod])
	return -1;

    mf->sample_rate = ac3_sample_rates[fscod];
    mf->bitrate = ac3_bitrates[frmsizecod] * 1000;
    mf->size = ac3_frame_sizes[frmsizecod][fscod] * 2;
    mf->samples = 6 * 256;

    mf->layer = 4;

    return 0;
}

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

#ifndef MATROSKA_H
#define MATROSKA_H

#define MATROSKA_ID_SEGMENT                     0x8538067

#define MATROSKA_ID_SEEKHEAD                    0x14d9b74
#define MATROSKA_ID_SEEK                        0xdbb
#define MATROSKA_ID_SEEKID                      0x13ab
#define MATROSKA_ID_SEEKPOSITION                0x13ac

#define MATROSKA_ID_INFO                        0x549a966
#define MATROSKA_ID_SEGMENTUID                  0x33a4
#define MATROSKA_ID_SEGMENTFILENAME             0x3384
#define MATROSKA_ID_PREVUID                     0x1cb923
#define MATROSKA_ID_PREVFILENAME                0x1c83ab
#define MATROSKA_ID_NEXTUID                     0x1eb923
#define MATROSKA_ID_NEXTFILENAME                0x1e83bb
#define MATROSKA_ID_TIMECODESCALE               0xad7b1
#define MATROSKA_ID_DURATION                    0x489
#define MATROSKA_ID_DATEUTC                     0x461
#define MATROSKA_ID_TITLE                       0x3ba9
#define MATROSKA_ID_MUXINGAPP                   0xd80
#define MATROSKA_ID_WRITINGAPP                  0x1741

#define MATROSKA_ID_CLUSTER                     0xf43b675
#define MATROSKA_ID_TIMECODE                    0x67
#define MATROSKA_ID_POSITION                    0x27
#define MATROSKA_ID_PREVSIZE                    0x2b
#define MATROSKA_ID_BLOCKGROUP                  0x20
#define MATROSKA_ID_SIMPLEBLOCK                 0x23

#define MATROSKA_ID_BLOCK                       0x21
#define MATROSKA_ID_BLOCKVIRTUAL                0x22
#define MATROSKA_ID_BLOCKADDITIONS              0x35a1
#define MATROSKA_ID_BLOCKMORE                   0x26
#define MATROSKA_ID_BLOCKADDID                  0x6e
#define MATROSKA_ID_BLOCKADDITIONAL             0x25
#define MATROSKA_ID_BLOCKDURATION               0x1b
#define MATROSKA_ID_REFERENCEPRIORITY           0x7a
#define MATROSKA_ID_REFERENCEBLOCK              0x7b
#define MATROSKA_ID_REFERENCEVIRTUAL            0x7d
#define MATROSKA_ID_CODECSTATE                  0x24

#define MATROSKA_ID_SLICES                      0xe
#define MATROSKA_ID_TIMESLICE                   0x68
#define MATROSKA_ID_LACENUMBER                  0x4c
#define MATROSKA_ID_FRAMENUMBER                 0x4d
#define MATROSKA_ID_BLOCKADDITIONID             0x4b
#define MATROSKA_ID_DELAY                       0x4e
#define MATROSKA_ID_SLICEDURATION               0x4f

#define MATROSKA_ID_TRACKS                      0x654ae6b
#define MATROSKA_ID_TRACKENTRY                  0x2e
#define MATROSKA_ID_TRACKNUMBER                 0x57
#define MATROSKA_ID_TRACKUID                    0x33c5
#define MATROSKA_ID_TRACKTYPE                   0x3
#define MATROSKA_ID_FLAGENABLED                 0x39
#define MATROSKA_ID_FLAGDEFAULT                 0x8
#define MATROSKA_ID_FLAGLACING                  0x1c
#define MATROSKA_ID_MINCACHE                    0x2de7
#define MATROSKA_ID_MAXCACHE                    0x2df8
#define MATROSKA_ID_DEFAULTDURATION             0x3e383
#define MATROSKA_ID_TRACKTIMECODESCALE          0x3314f
#define MATROSKA_ID_NAME                        0x136e
#define MATROSKA_ID_LANGUAGE                    0x2b59c
#define MATROSKA_ID_CODECID                     0x6
#define MATROSKA_ID_CODECPRIVATE                0x23a2
#define MATROSKA_ID_CODECNAME                   0x58688
#define MATROSKA_ID_CODECSETTINGS               0x1a9697
#define MATROSKA_ID_CODECINFOURL                0x1b4040
#define MATROSKA_ID_CODECDOWNLOADURL            0x6b240
#define MATROSKA_ID_CODECDECODEALL              0x2a
#define MATROSKA_ID_TRACKOVERLAY                0x2fab

#define MATROSKA_ID_VIDEO                       0x60
#define MATROSKA_ID_FLAGINTERLACED              0x1a
#define MATROSKA_ID_STEREOMODE                  0x13b8
#define MATROSKA_ID_PIXELWIDTH                  0x30
#define MATROSKA_ID_PIXELHEIGHT                 0x3a
#define MATROSKA_ID_PIXELCROPBOTTOM             0x14aa
#define MATROSKA_ID_PIXELCROPTOP                0x14bb
#define MATROSKA_ID_PIXELCROPLEFT               0x14cc
#define MATROSKA_ID_PIXELCROPRIGHT              0x14dd
#define MATROSKA_ID_DISPLAYWIDTH                0x14b0
#define MATROSKA_ID_DISPLAYHEIGHT               0x14ba
#define MATROSKA_ID_DISPLAYUNIT                 0x14b2
#define MATROSKA_ID_ASPECTRATIOTYPE             0x14b3
#define MATROSKA_ID_COLOURSPACE                 0xeb524
#define MATROSKA_ID_GAMMAVALUE                  0xfb523

#define MATROSKA_ID_AUDIO                       0x61
#define MATROSKA_ID_SAMPLINGFREQUENCY           0x35
#define MATROSKA_ID_OUTPUTSAMPLINGFREQUENCY     0x38b5
#define MATROSKA_ID_CHANNELS                    0x1f
#define MATROSKA_ID_CHANNELPOSITIONS            0x3d7b
#define MATROSKA_ID_BITDEPTH                    0x2264

#define MATROSKA_ID_CONTENTENCODINGS            0x2d80
#define MATROSKA_ID_CONTENTENCODING             0x2240
#define MATROSKA_ID_CONTENTENCODINGORDER        0x1031
#define MATROSKA_ID_CONTENTENCODINGSCOPE        0x1032
#define MATROSKA_ID_CONTENTENCODINGTYPE         0x1033
#define MATROSKA_ID_CONTENTCOMPRESSION          0x1034
#define MATROSKA_ID_CONTENTCOMPALGO             0x254
#define MATROSKA_ID_CONTENTCOMPSETTINGS         0x255
#define MATROSKA_ID_CONTENTENCRYPTION           0x1035
#define MATROSKA_ID_CONTENTENCALGO              0x7e1
#define MATROSKA_ID_CONTENTENCKEYID             0x7e2
#define MATROSKA_ID_CONTENTSIGNATURE            0x7e3
#define MATROSKA_ID_CONTENTSIGKEYID             0x7e4
#define MATROSKA_ID_CONTENTSIGALGO              0x7e5
#define MATROSKA_ID_CONTENTSIGHASHALGO          0x7e6

#define MATROSKA_ID_CUES                        0xc53bb6b
#define MATROSKA_ID_CUEPOINT                    0x3b
#define MATROSKA_ID_CUETIME                     0x33
#define MATROSKA_ID_CUETRACKPOSITIONS           0x37
#define MATROSKA_ID_CUETRACK                    0x77
#define MATROSKA_ID_CUECLUSTERPOSITION          0x71
#define MATROSKA_ID_CUEBLOCKNUMBER              0x1378
#define MATROSKA_ID_CUECODECSTATE               0x6a
#define MATROSKA_ID_CUEREFERENCE                0x5b
#define MATROSKA_ID_CUEREFTIME                  0x16
#define MATROSKA_ID_CUEREFCLUSTER               0x17
#define MATROSKA_ID_CUEREFNUMBER                0x135f
#define MATROSKA_ID_CUEREFCODECSTATE            0x6b

#define MATROSKA_ID_ATTACHMENTS                 0x941a469
#define MATROSKA_ID_ATTACHEDFILE                0x21a7
#define MATROSKA_ID_FILEDESCRIPTION             0x67e
#define MATROSKA_ID_FILENAME                    0x66e
#define MATROSKA_ID_FILEMIMETYPE                0x660
#define MATROSKA_ID_FILEDATA                    0x65c
#define MATROSKA_ID_FILEUID                     0x6ae

#define MATROSKA_ID_CHAPTERS                    0x43a770
#define MATROSKA_ID_EDITIONENTRY                0x5b9
#define MATROSKA_ID_CHAPTERATOM                 0x36
#define MATROSKA_ID_CHAPTERUID                  0x33c4
#define MATROSKA_ID_CHAPTERTIMESTART            0x11
#define MATROSKA_ID_CHAPTERTIMEEND              0x12
#define MATROSKA_ID_CHAPTERFLAGHIDDEN           0x18
#define MATROSKA_ID_CHAPTERFLAGENABLED          0x598
#define MATROSKA_ID_CHAPTERTRACK                0xf
#define MATROSKA_ID_CHAPTERTRACKNUMBER          0x9
#define MATROSKA_ID_CHAPTERDISPLAY              0
#define MATROSKA_ID_CHAPSTRING                  0x5
#define MATROSKA_ID_CHAPLANGUAGE                0x37c
#define MATROSKA_ID_CHAPCOUNTRY                 0x37e

#define MATROSKA_ID_TAGS                        0x254c367
#define MATROSKA_ID_TAG                         0x3373
#define MATROSKA_ID_TARGETS                     0x23c0
#define MATROSKA_ID_TARGETTRACKUID              0x23c5
#define MATROSKA_ID_TARGETCHAPTERUID            0x23c4
#define MATROSKA_ID_ATTACHMENTUID               0x23c6
#define MATROSKA_ID_SIMPLETAG                   0x27c8
#define MATROSKA_ID_TAGNAME                     0x5a3
#define MATROSKA_ID_TAGSTRING                   0x487
#define MATROSKA_ID_TAGBINARY                   0x485

#endif

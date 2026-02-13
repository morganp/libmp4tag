/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef MP4_ATOMS_H
#define MP4_ATOMS_H

#include <tag_common/file_io.h>
#include <tag_common/buffer.h>
#include "../util/mp4_buffer_ext.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MP4 box (atom) header.
 * Standard box: 4-byte size + 4-byte type = 8 bytes header.
 * Extended box: 4-byte size (==1) + 4-byte type + 8-byte ext size = 16 bytes.
 * Size-to-end:  4-byte size (==0) + 4-byte type = box extends to EOF.
 */
typedef struct {
    uint32_t type;          /* FourCC as a big-endian uint32 */
    int64_t  offset;        /* File offset of the box start */
    int64_t  size;          /* Total box size (header + data), -1 if to-EOF */
    int64_t  data_offset;   /* File offset of the box payload */
    int64_t  data_size;     /* Payload size, -1 if to-EOF */
    int      header_size;   /* 8 or 16 */
} mp4_box_t;

/* Create a FourCC from a string literal */
#define MP4_FOURCC(a,b,c,d) \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | \
     ((uint32_t)(uint8_t)(c) << 8)  |  (uint32_t)(uint8_t)(d))

/* Well-known box types */
#define MP4_BOX_FTYP  MP4_FOURCC('f','t','y','p')
#define MP4_BOX_MOOV  MP4_FOURCC('m','o','o','v')
#define MP4_BOX_MDAT  MP4_FOURCC('m','d','a','t')
#define MP4_BOX_FREE  MP4_FOURCC('f','r','e','e')
#define MP4_BOX_SKIP  MP4_FOURCC('s','k','i','p')
#define MP4_BOX_UDTA  MP4_FOURCC('u','d','t','a')
#define MP4_BOX_META  MP4_FOURCC('m','e','t','a')
#define MP4_BOX_ILST  MP4_FOURCC('i','l','s','t')
#define MP4_BOX_HDLR  MP4_FOURCC('h','d','l','r')
#define MP4_BOX_DATA  MP4_FOURCC('d','a','t','a')
#define MP4_BOX_TRAK  MP4_FOURCC('t','r','a','k')
#define MP4_BOX_MDIA  MP4_FOURCC('m','d','i','a')
#define MP4_BOX_MVHD  MP4_FOURCC('m','v','h','d')

/* iTunes-specific tag atom types */
#define MP4_TAG_NAM   MP4_FOURCC(0xA9,'n','a','m')  /* Title */
#define MP4_TAG_ART   MP4_FOURCC(0xA9,'A','R','T')  /* Artist */
#define MP4_TAG_ALB   MP4_FOURCC(0xA9,'a','l','b')  /* Album */
#define MP4_TAG_AART  MP4_FOURCC('a','A','R','T')    /* Album artist */
#define MP4_TAG_DAY   MP4_FOURCC(0xA9,'d','a','y')  /* Year/Date */
#define MP4_TAG_TRKN  MP4_FOURCC('t','r','k','n')    /* Track number */
#define MP4_TAG_DISK  MP4_FOURCC('d','i','s','k')    /* Disc number */
#define MP4_TAG_GEN   MP4_FOURCC(0xA9,'g','e','n')  /* Genre (text) */
#define MP4_TAG_GNRE  MP4_FOURCC('g','n','r','e')    /* Genre (ID3v1 num) */
#define MP4_TAG_WRT   MP4_FOURCC(0xA9,'w','r','t')  /* Composer */
#define MP4_TAG_CMT   MP4_FOURCC(0xA9,'c','m','t')  /* Comment */
#define MP4_TAG_TOO   MP4_FOURCC(0xA9,'t','o','o')  /* Encoder */
#define MP4_TAG_CPRT  MP4_FOURCC('c','p','r','t')    /* Copyright */
#define MP4_TAG_TMPO  MP4_FOURCC('t','m','p','o')    /* BPM */
#define MP4_TAG_LYR   MP4_FOURCC(0xA9,'l','y','r')  /* Lyrics */
#define MP4_TAG_GRP   MP4_FOURCC(0xA9,'g','r','p')  /* Grouping */
#define MP4_TAG_DESC  MP4_FOURCC('d','e','s','c')    /* Description */
#define MP4_TAG_COVR  MP4_FOURCC('c','o','v','r')    /* Cover art */
#define MP4_TAG_CPIL  MP4_FOURCC('c','p','i','l')    /* Compilation */
#define MP4_TAG_PGAP  MP4_FOURCC('p','g','a','p')    /* Gapless playback */
#define MP4_TAG_SONM  MP4_FOURCC('s','o','n','m')    /* Sort title */
#define MP4_TAG_SOAR  MP4_FOURCC('s','o','a','r')    /* Sort artist */
#define MP4_TAG_SOAL  MP4_FOURCC('s','o','a','l')    /* Sort album */
#define MP4_TAG_SOAA  MP4_FOURCC('s','o','a','a')    /* Sort album artist */
#define MP4_TAG_SOCO  MP4_FOURCC('s','o','c','o')    /* Sort composer */

/* iTunes data type flags (in the 'data' box) */
#define MP4_DATA_UTF8     1
#define MP4_DATA_UTF16    2
#define MP4_DATA_JPEG    13
#define MP4_DATA_PNG     14
#define MP4_DATA_INTEGER 21
#define MP4_DATA_IMPLICIT 0

/* Read a box header at the current file position. */
int mp4_read_box_header(file_handle_t *fh, mp4_box_t *box);

/* Convert a FourCC to a string (writes 5 bytes including NUL). */
void mp4_fourcc_to_str(uint32_t fourcc, char out[5]);

/* Convert a string to a FourCC. */
uint32_t mp4_str_to_fourcc(const char *s);

/* Write a standard 8-byte box header to a buffer. */
int mp4_write_box_header(dyn_buffer_t *buf, uint32_t type, uint32_t size);

/* Write a 'free' box of the given total size (including 8-byte header). */
int mp4_write_free_box(dyn_buffer_t *buf, uint32_t total_size);

#ifdef __cplusplus
}
#endif

#endif /* MP4_ATOMS_H */

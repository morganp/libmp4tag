/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef MP4_PARSER_H
#define MP4_PARSER_H

#include "mp4_atoms.h"
#include "../io/file_io.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parsed MP4 file structure — positions of key boxes.
 */
typedef struct {
    int     valid;              /* Non-zero if the file was successfully parsed */

    /* ftyp */
    int64_t ftyp_offset;

    /* moov box */
    int64_t moov_offset;
    int64_t moov_size;          /* Total moov box size */

    /* moov > udta */
    int     has_udta;
    int64_t udta_offset;
    int64_t udta_size;

    /* moov > udta > meta */
    int     has_meta;
    int64_t meta_offset;
    int64_t meta_size;
    int     meta_has_hdlr;      /* meta box has handler reference box */

    /* moov > udta > meta > ilst */
    int     has_ilst;
    int64_t ilst_offset;
    int64_t ilst_size;

    /* Free space after ilst (or after meta/udta) */
    int     has_free_after_ilst;
    int64_t free_after_ilst_offset;
    int64_t free_after_ilst_size;

    /* mdat position (needed for rewrite) */
    int64_t mdat_offset;
    int64_t mdat_size;
} mp4_file_info_t;

/*
 * Validate that a file is an MP4/M4A/M4V type by checking the ftyp box.
 * Returns MP4TAG_OK if valid.
 */
int mp4_validate_ftyp(file_handle_t *fh);

/*
 * Parse the top-level box structure of an MP4 file and locate
 * moov, udta, meta, ilst, and free boxes.
 */
int mp4_parse_structure(file_handle_t *fh, mp4_file_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* MP4_PARSER_H */

/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "mp4_atoms.h"
#include "../../include/mp4tag/mp4tag_error.h"

#include <string.h>

int mp4_read_box_header(file_handle_t *fh, mp4_box_t *box)
{
    if (!fh || !box) return MP4TAG_ERR_INVALID_ARG;

    box->offset = file_tell(fh);
    if (box->offset < 0) return MP4TAG_ERR_IO;

    uint8_t hdr[8];
    int rc = file_read(fh, hdr, 8);
    if (rc != 0) return rc;

    uint32_t raw_size = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
                        ((uint32_t)hdr[2] << 8)  |  (uint32_t)hdr[3];
    box->type = ((uint32_t)hdr[4] << 24) | ((uint32_t)hdr[5] << 16) |
                ((uint32_t)hdr[6] << 8)  |  (uint32_t)hdr[7];

    if (raw_size == 1) {
        /* Extended 64-bit size */
        uint8_t ext[8];
        rc = file_read(fh, ext, 8);
        if (rc != 0) return rc;

        box->size = ((int64_t)ext[0] << 56) | ((int64_t)ext[1] << 48) |
                    ((int64_t)ext[2] << 40) | ((int64_t)ext[3] << 32) |
                    ((int64_t)ext[4] << 24) | ((int64_t)ext[5] << 16) |
                    ((int64_t)ext[6] << 8)  |  (int64_t)ext[7];
        box->header_size = 16;
    } else if (raw_size == 0) {
        /* Box extends to end of file */
        int64_t fs = file_size(fh);
        if (fs < 0) return MP4TAG_ERR_IO;
        box->size = fs - box->offset;
        box->header_size = 8;
    } else {
        box->size = raw_size;
        box->header_size = 8;
    }

    box->data_offset = box->offset + box->header_size;
    box->data_size   = box->size - box->header_size;

    return MP4TAG_OK;
}

void mp4_fourcc_to_str(uint32_t fourcc, char out[5])
{
    out[0] = (char)((fourcc >> 24) & 0xFF);
    out[1] = (char)((fourcc >> 16) & 0xFF);
    out[2] = (char)((fourcc >> 8)  & 0xFF);
    out[3] = (char)( fourcc        & 0xFF);
    out[4] = '\0';
}

uint32_t mp4_str_to_fourcc(const char *s)
{
    if (!s) return 0;
    size_t len = strlen(s);
    uint8_t a = len > 0 ? (uint8_t)s[0] : 0;
    uint8_t b = len > 1 ? (uint8_t)s[1] : 0;
    uint8_t c = len > 2 ? (uint8_t)s[2] : 0;
    uint8_t d = len > 3 ? (uint8_t)s[3] : 0;
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
           ((uint32_t)c << 8)  |  (uint32_t)d;
}

int mp4_write_box_header(dyn_buffer_t *buf, uint32_t type, uint32_t size)
{
    if (buffer_append_be32(buf, size) != 0) return -1;
    if (buffer_append_be32(buf, type) != 0) return -1;
    return 0;
}

int mp4_write_free_box(dyn_buffer_t *buf, uint32_t total_size)
{
    if (total_size < 8) return -1;
    if (mp4_write_box_header(buf, MP4_BOX_FREE, total_size) != 0) return -1;
    if (buffer_append_zeros(buf, total_size - 8) != 0) return -1;
    return 0;
}

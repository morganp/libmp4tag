/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "mp4_parser.h"
#include "../../include/mp4tag/mp4tag_error.h"

#include <string.h>

int mp4_validate_ftyp(file_handle_t *fh)
{
    if (!fh) return MP4TAG_ERR_INVALID_ARG;

    int rc = file_seek(fh, 0);
    if (rc != 0) return MP4TAG_ERR_SEEK_FAILED;

    mp4_box_t box;
    rc = mp4_read_box_header(fh, &box);
    if (rc != 0) return MP4TAG_ERR_NOT_MP4;

    if (box.type != MP4_BOX_FTYP)
        return MP4TAG_ERR_NOT_MP4;

    /* Read major brand (4 bytes) */
    if (box.data_size < 4)
        return MP4TAG_ERR_NOT_MP4;

    uint8_t brand[4];
    rc = file_read(fh, brand, 4);
    if (rc != 0) return MP4TAG_ERR_NOT_MP4;

    /*
     * Accept common MP4/M4A/M4V brands:
     *   isom, iso2, iso5, iso6, mp41, mp42, M4A , M4B , M4P , M4V ,
     *   avc1, f4v , qt  , MSNV, NDAS, dash
     * Also accept 3gp/3g2 variants.
     */
    uint32_t major = ((uint32_t)brand[0] << 24) | ((uint32_t)brand[1] << 16) |
                     ((uint32_t)brand[2] << 8)  |  (uint32_t)brand[3];

    switch (major) {
    case MP4_FOURCC('i','s','o','m'):
    case MP4_FOURCC('i','s','o','2'):
    case MP4_FOURCC('i','s','o','5'):
    case MP4_FOURCC('i','s','o','6'):
    case MP4_FOURCC('m','p','4','1'):
    case MP4_FOURCC('m','p','4','2'):
    case MP4_FOURCC('M','4','A',' '):
    case MP4_FOURCC('M','4','B',' '):
    case MP4_FOURCC('M','4','P',' '):
    case MP4_FOURCC('M','4','V',' '):
    case MP4_FOURCC('M','4','V','H'):
    case MP4_FOURCC('a','v','c','1'):
    case MP4_FOURCC('f','4','v',' '):
    case MP4_FOURCC('q','t',' ',' '):
    case MP4_FOURCC('M','S','N','V'):
    case MP4_FOURCC('d','a','s','h'):
    case MP4_FOURCC('3','g','p','4'):
    case MP4_FOURCC('3','g','p','5'):
    case MP4_FOURCC('3','g','p','6'):
    case MP4_FOURCC('3','g','2','a'):
        return MP4TAG_OK;
    default:
        break;
    }

    /*
     * If the major brand isn't recognized, scan compatible brands.
     * This handles files whose major brand is unusual but list
     * a recognized brand in their compatible list.
     */
    if (box.data_size >= 12) {
        /* Skip major brand (4) + minor version (4) = 8 bytes from data start */
        int64_t compat_offset = box.data_offset + 8;
        int64_t compat_end = box.data_offset + box.data_size;
        rc = file_seek(fh, compat_offset);
        if (rc != 0) return MP4TAG_ERR_NOT_MP4;

        while (compat_offset + 4 <= compat_end) {
            uint8_t cb[4];
            rc = file_read(fh, cb, 4);
            if (rc != 0) break;
            uint32_t compat = ((uint32_t)cb[0] << 24) | ((uint32_t)cb[1] << 16) |
                              ((uint32_t)cb[2] << 8)  |  (uint32_t)cb[3];
            switch (compat) {
            case MP4_FOURCC('i','s','o','m'):
            case MP4_FOURCC('m','p','4','1'):
            case MP4_FOURCC('m','p','4','2'):
            case MP4_FOURCC('M','4','A',' '):
            case MP4_FOURCC('M','4','B',' '):
            case MP4_FOURCC('M','4','V',' '):
            case MP4_FOURCC('a','v','c','1'):
                return MP4TAG_OK;
            default:
                break;
            }
            compat_offset += 4;
        }
    }

    return MP4TAG_ERR_NOT_MP4;
}

/*
 * Scan children of a container box looking for a specific type.
 * Returns MP4TAG_OK and fills `found` if the box is found.
 */
static int find_child_box(file_handle_t *fh, int64_t parent_data_offset,
                          int64_t parent_data_size, uint32_t target_type,
                          mp4_box_t *found)
{
    int64_t pos = parent_data_offset;
    int64_t end = parent_data_offset + parent_data_size;

    while (pos + 8 <= end) {
        int rc = file_seek(fh, pos);
        if (rc != 0) return rc;

        mp4_box_t child;
        rc = mp4_read_box_header(fh, &child);
        if (rc != 0) return rc;

        if (child.size < 8) return MP4TAG_ERR_CORRUPT;

        if (child.type == target_type) {
            *found = child;
            return MP4TAG_OK;
        }

        pos = child.offset + child.size;
    }

    return MP4TAG_ERR_TAG_NOT_FOUND;
}

/*
 * Look for a 'free' box immediately following a box at the given end offset.
 * The search is bounded by `container_end`.
 */
static int find_free_after(file_handle_t *fh, int64_t after_offset,
                           int64_t container_end,
                           int64_t *free_offset, int64_t *free_size)
{
    if (after_offset + 8 > container_end)
        return MP4TAG_ERR_TAG_NOT_FOUND;

    int rc = file_seek(fh, after_offset);
    if (rc != 0) return rc;

    mp4_box_t box;
    rc = mp4_read_box_header(fh, &box);
    if (rc != 0) return rc;

    if (box.type == MP4_BOX_FREE || box.type == MP4_BOX_SKIP) {
        *free_offset = box.offset;
        *free_size   = box.size;
        return MP4TAG_OK;
    }

    return MP4TAG_ERR_TAG_NOT_FOUND;
}

int mp4_parse_structure(file_handle_t *fh, mp4_file_info_t *info)
{
    if (!fh || !info) return MP4TAG_ERR_INVALID_ARG;

    memset(info, 0, sizeof(*info));
    info->ftyp_offset = -1;
    info->moov_offset = -1;
    info->mdat_offset = -1;

    int64_t fsize = file_size(fh);
    if (fsize < 8) return MP4TAG_ERR_TRUNCATED;

    /* Scan top-level boxes */
    int64_t pos = 0;
    while (pos + 8 <= fsize) {
        int rc = file_seek(fh, pos);
        if (rc != 0) return rc;

        mp4_box_t box;
        rc = mp4_read_box_header(fh, &box);
        if (rc != 0) break;

        if (box.size < 8) break;

        switch (box.type) {
        case MP4_BOX_FTYP:
            info->ftyp_offset = box.offset;
            break;
        case MP4_BOX_MOOV:
            info->moov_offset = box.offset;
            info->moov_size   = box.size;
            break;
        case MP4_BOX_MDAT:
            info->mdat_offset = box.offset;
            info->mdat_size   = box.size;
            break;
        default:
            break;
        }

        pos = box.offset + box.size;
    }

    if (info->moov_offset < 0)
        return MP4TAG_ERR_NOT_MP4;

    /* Parse moov to find udta */
    {
        mp4_box_t moov;
        int rc = file_seek(fh, info->moov_offset);
        if (rc != 0) return rc;
        rc = mp4_read_box_header(fh, &moov);
        if (rc != 0) return rc;

        mp4_box_t udta;
        rc = find_child_box(fh, moov.data_offset, moov.data_size,
                            MP4_BOX_UDTA, &udta);
        if (rc == MP4TAG_OK) {
            info->has_udta    = 1;
            info->udta_offset = udta.offset;
            info->udta_size   = udta.size;

            /* Parse udta to find meta */
            mp4_box_t meta;
            rc = find_child_box(fh, udta.data_offset, udta.data_size,
                                MP4_BOX_META, &meta);
            if (rc == MP4TAG_OK) {
                info->has_meta    = 1;
                info->meta_offset = meta.offset;
                info->meta_size   = meta.size;

                /*
                 * The 'meta' box is a "full box" with 4 extra bytes
                 * (version + flags) after the standard header.
                 */
                int64_t meta_content_offset = meta.data_offset + 4;
                int64_t meta_content_size   = meta.data_size - 4;
                if (meta_content_size < 0) meta_content_size = 0;

                /* Check for hdlr */
                mp4_box_t hdlr;
                rc = find_child_box(fh, meta_content_offset, meta_content_size,
                                    MP4_BOX_HDLR, &hdlr);
                info->meta_has_hdlr = (rc == MP4TAG_OK);

                /* Find ilst */
                mp4_box_t ilst;
                rc = find_child_box(fh, meta_content_offset, meta_content_size,
                                    MP4_BOX_ILST, &ilst);
                if (rc == MP4TAG_OK) {
                    info->has_ilst    = 1;
                    info->ilst_offset = ilst.offset;
                    info->ilst_size   = ilst.size;

                    /* Look for free space after ilst */
                    int64_t after_ilst = ilst.offset + ilst.size;
                    int64_t meta_end   = meta.offset + meta.size;
                    rc = find_free_after(fh, after_ilst, meta_end,
                                         &info->free_after_ilst_offset,
                                         &info->free_after_ilst_size);
                    info->has_free_after_ilst = (rc == MP4TAG_OK);
                }
            }

            /*
             * Note: we intentionally do NOT look for free space after
             * udta within moov, because that space is not contiguous
             * with ilst and cannot be used for simple in-place writes.
             * Non-contiguous cases fall through to the full rewrite path.
             */
        }
    }

    info->valid = 1;
    return MP4TAG_OK;
}

/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "../include/mp4tag/mp4tag.h"
#include "mp4/mp4_parser.h"
#include "mp4/mp4_tags.h"
#include "mp4/mp4_atoms.h"
#include "io/file_io.h"
#include "util/buffer.h"
#include "util/string_util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  Internal context definition                                        */
/* ------------------------------------------------------------------ */

struct mp4tag_context {
    mp4tag_allocator_t  allocator;
    int                 has_allocator;

    file_handle_t      *fh;
    char               *path;
    int                 writable;

    /* Parsed file structure */
    mp4_file_info_t     info;

    /* Cached tag collection (owned by context) */
    mp4tag_collection_t *cached_tags;
};

/* ------------------------------------------------------------------ */
/*  Cache helpers                                                      */
/* ------------------------------------------------------------------ */

static void invalidate_cache(mp4tag_context_t *ctx)
{
    if (ctx->cached_tags) {
        mp4_tags_free_collection(ctx->cached_tags);
        ctx->cached_tags = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  Version / Error                                                    */
/* ------------------------------------------------------------------ */

const char *mp4tag_version(void)
{
    return "1.0.0";
}

const char *mp4tag_strerror(int error)
{
    switch (error) {
    case MP4TAG_OK:                return "Success";
    case MP4TAG_ERR_INVALID_ARG:   return "Invalid argument";
    case MP4TAG_ERR_NO_MEMORY:     return "Out of memory";
    case MP4TAG_ERR_IO:            return "I/O error";
    case MP4TAG_ERR_NOT_OPEN:      return "File not open";
    case MP4TAG_ERR_ALREADY_OPEN:  return "File already open";
    case MP4TAG_ERR_READ_ONLY:     return "File opened read-only";
    case MP4TAG_ERR_NOT_MP4:       return "Not a supported MP4 file";
    case MP4TAG_ERR_BAD_BOX:       return "Invalid box structure";
    case MP4TAG_ERR_CORRUPT:       return "File is corrupted";
    case MP4TAG_ERR_TRUNCATED:     return "Unexpected end of file";
    case MP4TAG_ERR_UNSUPPORTED:   return "Unsupported format";
    case MP4TAG_ERR_NO_TAGS:       return "No tags found";
    case MP4TAG_ERR_TAG_NOT_FOUND: return "Tag not found";
    case MP4TAG_ERR_TAG_TOO_LARGE: return "Tag data too large for buffer";
    case MP4TAG_ERR_NO_SPACE:      return "Not enough space for in-place write";
    case MP4TAG_ERR_WRITE_FAILED:  return "Write operation failed";
    case MP4TAG_ERR_SEEK_FAILED:   return "Seek operation failed";
    case MP4TAG_ERR_RENAME_FAILED: return "File rename failed";
    default:                       return "Unknown error";
    }
}

/* ------------------------------------------------------------------ */
/*  Context lifecycle                                                  */
/* ------------------------------------------------------------------ */

mp4tag_context_t *mp4tag_create(const mp4tag_allocator_t *allocator)
{
    mp4tag_context_t *ctx;

    if (allocator && allocator->alloc) {
        ctx = allocator->alloc(sizeof(*ctx), allocator->user_data);
        if (!ctx) return NULL;
        memset(ctx, 0, sizeof(*ctx));
        ctx->allocator     = *allocator;
        ctx->has_allocator = 1;
    } else {
        ctx = calloc(1, sizeof(*ctx));
    }

    return ctx;
}

void mp4tag_destroy(mp4tag_context_t *ctx)
{
    if (!ctx) return;
    mp4tag_close(ctx);

    if (ctx->has_allocator && ctx->allocator.free)
        ctx->allocator.free(ctx, ctx->allocator.user_data);
    else
        free(ctx);
}

int mp4tag_open(mp4tag_context_t *ctx, const char *path)
{
    if (!ctx || !path)           return MP4TAG_ERR_INVALID_ARG;
    if (ctx->fh)                 return MP4TAG_ERR_ALREADY_OPEN;

    ctx->fh = file_open_read(path);
    if (!ctx->fh)                return MP4TAG_ERR_IO;

    ctx->path     = str_dup(path);
    ctx->writable = 0;

    /* Validate file type */
    int rc = mp4_validate_ftyp(ctx->fh);
    if (rc != MP4TAG_OK) {
        mp4tag_close(ctx);
        return rc;
    }

    /* Parse structure */
    rc = mp4_parse_structure(ctx->fh, &ctx->info);
    if (rc != MP4TAG_OK) {
        mp4tag_close(ctx);
        return rc;
    }

    return MP4TAG_OK;
}

int mp4tag_open_rw(mp4tag_context_t *ctx, const char *path)
{
    if (!ctx || !path)           return MP4TAG_ERR_INVALID_ARG;
    if (ctx->fh)                 return MP4TAG_ERR_ALREADY_OPEN;

    ctx->fh = file_open_rw(path);
    if (!ctx->fh)                return MP4TAG_ERR_IO;

    ctx->path     = str_dup(path);
    ctx->writable = 1;

    int rc = mp4_validate_ftyp(ctx->fh);
    if (rc != MP4TAG_OK) {
        mp4tag_close(ctx);
        return rc;
    }

    rc = mp4_parse_structure(ctx->fh, &ctx->info);
    if (rc != MP4TAG_OK) {
        mp4tag_close(ctx);
        return rc;
    }

    return MP4TAG_OK;
}

void mp4tag_close(mp4tag_context_t *ctx)
{
    if (!ctx) return;
    invalidate_cache(ctx);
    if (ctx->fh) {
        file_close(ctx->fh);
        ctx->fh = NULL;
    }
    free(ctx->path);
    ctx->path     = NULL;
    ctx->writable = 0;
    memset(&ctx->info, 0, sizeof(ctx->info));
}

int mp4tag_is_open(const mp4tag_context_t *ctx)
{
    return (ctx && ctx->fh) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  Tag reading                                                        */
/* ------------------------------------------------------------------ */

int mp4tag_read_tags(mp4tag_context_t *ctx, mp4tag_collection_t **tags)
{
    if (!ctx || !tags)     return MP4TAG_ERR_INVALID_ARG;
    if (!ctx->fh)          return MP4TAG_ERR_NOT_OPEN;

    if (ctx->cached_tags) {
        *tags = ctx->cached_tags;
        return MP4TAG_OK;
    }

    if (!ctx->info.has_ilst)
        return MP4TAG_ERR_NO_TAGS;

    mp4tag_collection_t *coll = NULL;
    int rc = mp4_tags_parse_ilst(ctx->fh, &ctx->info, &coll);
    if (rc != MP4TAG_OK)
        return rc;

    ctx->cached_tags = coll;
    *tags = coll;
    return MP4TAG_OK;
}

int mp4tag_read_tag_string(mp4tag_context_t *ctx, const char *name,
                           char *value, size_t size)
{
    if (!ctx || !name || !value || size == 0)
        return MP4TAG_ERR_INVALID_ARG;

    mp4tag_collection_t *coll = NULL;
    int rc = mp4tag_read_tags(ctx, &coll);
    if (rc != MP4TAG_OK) return rc;

    for (const mp4tag_tag_t *tag = coll->tags; tag; tag = tag->next) {
        for (const mp4tag_simple_tag_t *st = tag->simple_tags; st; st = st->next) {
            if (st->name && st->value && str_casecmp(st->name, name) == 0) {
                return str_copy(value, size, st->value) == 0
                       ? MP4TAG_OK : MP4TAG_ERR_TAG_TOO_LARGE;
            }
        }
    }

    return MP4TAG_ERR_TAG_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/*  Write helpers                                                      */
/* ------------------------------------------------------------------ */

static int write_zeros(file_handle_t *fh, uint32_t count)
{
    uint8_t zeros[4096];
    memset(zeros, 0, sizeof(zeros));
    while (count > 0) {
        uint32_t chunk = count < sizeof(zeros) ? count : (uint32_t)sizeof(zeros);
        if (file_write(fh, zeros, chunk) != 0)
            return MP4TAG_ERR_WRITE_FAILED;
        count -= chunk;
    }
    return MP4TAG_OK;
}

/*
 * Strategy 1: In-place replacement.
 * Replace the ilst content within the existing udta/meta structure,
 * using any adjacent 'free' box as extra space.
 */
static int try_inplace(mp4tag_context_t *ctx, dyn_buffer_t *ilst_content)
{
    mp4_file_info_t *info = &ctx->info;
    if (!info->has_ilst)
        return MP4TAG_ERR_NO_SPACE;

    /* New ilst box size */
    uint32_t new_ilst_size = 8 + (uint32_t)ilst_content->size;

    /* Available space = existing ilst + any trailing free */
    int64_t available = info->ilst_size;
    if (info->has_free_after_ilst)
        available += info->free_after_ilst_size;

    if ((int64_t)new_ilst_size > available)
        return MP4TAG_ERR_NO_SPACE;

    /* Write new ilst at the existing ilst offset */
    int rc = file_seek(ctx->fh, info->ilst_offset);
    if (rc != 0) return MP4TAG_ERR_SEEK_FAILED;

    /* ilst header */
    dyn_buffer_t ilst_box;
    buffer_init(&ilst_box);
    mp4_write_box_header(&ilst_box, MP4_BOX_ILST, new_ilst_size);
    rc = file_write(ctx->fh, ilst_box.data, ilst_box.size);
    buffer_free(&ilst_box);
    if (rc != 0) return rc;

    /* ilst content */
    rc = file_write(ctx->fh, ilst_content->data, ilst_content->size);
    if (rc != 0) return rc;

    /* Fill remaining space with a free box */
    int64_t remaining = available - new_ilst_size;
    if (remaining >= 8) {
        dyn_buffer_t free_box;
        buffer_init(&free_box);
        mp4_write_free_box(&free_box, (uint32_t)remaining);
        rc = file_write(ctx->fh, free_box.data, free_box.size);
        buffer_free(&free_box);
        if (rc != 0) return rc;
    } else if (remaining > 0) {
        /* Too small for a free box, just zero-fill */
        rc = write_zeros(ctx->fh, (uint32_t)remaining);
        if (rc != 0) return rc;
    }

    /*
     * Update parent box sizes:
     * The total space consumed hasn't changed (we filled with free),
     * but the ilst size within meta may have changed.
     * Since we keep total space constant, parent sizes stay the same.
     *
     * However, if the ilst was previously smaller and there was free space
     * that we consumed, the meta/udta/moov sizes don't change since the
     * total footprint is the same.
     */

    file_sync(ctx->fh);

    /* Re-parse to update info */
    mp4_parse_structure(ctx->fh, &ctx->info);

    return MP4TAG_OK;
}

/*
 * Strategy 2: Rewrite the file.
 * Write to a temp file, then rename. This handles the case where moov
 * needs to grow.
 */
static int rewrite_file(mp4tag_context_t *ctx, dyn_buffer_t *udta_buf)
{
    if (!ctx->path)
        return MP4TAG_ERR_INVALID_ARG;

    size_t path_len = strlen(ctx->path);
    char *tmp_path = malloc(path_len + 5);
    if (!tmp_path) return MP4TAG_ERR_NO_MEMORY;
    memcpy(tmp_path, ctx->path, path_len);
    memcpy(tmp_path + path_len, ".tmp", 5);

    /* Create temp file */
    {
        FILE *f = fopen(tmp_path, "wb");
        if (!f) { free(tmp_path); return MP4TAG_ERR_IO; }
        fclose(f);
    }

    file_handle_t *tmp = file_open_rw(tmp_path);
    if (!tmp) { free(tmp_path); return MP4TAG_ERR_IO; }

    int result = MP4TAG_OK;
    int64_t src_size = file_size(ctx->fh);

    /*
     * Strategy: copy the file box by box.
     * - Copy ftyp and any boxes before moov as-is.
     * - For moov, rebuild it: copy all children except udta, then append new udta.
     * - Copy mdat and everything else as-is.
     */

    /* Pass 1: Collect top-level box positions */
    int64_t pos = 0;

    while (pos < src_size) {
        int rc = file_seek(ctx->fh, pos);
        if (rc != 0) { result = MP4TAG_ERR_SEEK_FAILED; goto cleanup; }

        mp4_box_t box;
        rc = mp4_read_box_header(ctx->fh, &box);
        if (rc != 0) break;
        if (box.size < 8) break;

        if (box.type == MP4_BOX_MOOV) {
            /*
             * Rebuild moov:
             * 1. Collect all moov children except udta
             * 2. Calculate new moov size
             * 3. Write moov header + children + new udta
             */

            /* First pass: calculate total size of non-udta children */
            int64_t moov_child_pos = box.data_offset;
            int64_t moov_end = box.offset + box.size;
            int64_t non_udta_size = 0;

            while (moov_child_pos + 8 <= moov_end) {
                rc = file_seek(ctx->fh, moov_child_pos);
                if (rc != 0) break;
                mp4_box_t child;
                rc = mp4_read_box_header(ctx->fh, &child);
                if (rc != 0 || child.size < 8) break;

                if (child.type != MP4_BOX_UDTA)
                    non_udta_size += child.size;

                moov_child_pos = child.offset + child.size;
            }

            uint32_t new_moov_size = 8 + (uint32_t)non_udta_size +
                                     (uint32_t)udta_buf->size;

            /* Write moov header */
            dyn_buffer_t moov_hdr;
            buffer_init(&moov_hdr);
            mp4_write_box_header(&moov_hdr, MP4_BOX_MOOV, new_moov_size);
            rc = file_write(tmp, moov_hdr.data, moov_hdr.size);
            buffer_free(&moov_hdr);
            if (rc != 0) { result = rc; goto cleanup; }

            /* Copy non-udta children */
            moov_child_pos = box.data_offset;
            while (moov_child_pos + 8 <= moov_end) {
                rc = file_seek(ctx->fh, moov_child_pos);
                if (rc != 0) break;
                mp4_box_t child;
                rc = mp4_read_box_header(ctx->fh, &child);
                if (rc != 0 || child.size < 8) break;

                if (child.type != MP4_BOX_UDTA) {
                    /* Copy this child box */
                    rc = file_seek(ctx->fh, child.offset);
                    if (rc != 0) { result = rc; goto cleanup; }

                    int64_t bytes_left = child.size;
                    uint8_t copy_buf[65536];
                    while (bytes_left > 0) {
                        size_t to_read = (size_t)(bytes_left < (int64_t)sizeof(copy_buf)
                                                  ? bytes_left : (int64_t)sizeof(copy_buf));
                        int64_t n = file_read_partial(ctx->fh, copy_buf, to_read);
                        if (n <= 0) break;
                        rc = file_write(tmp, copy_buf, (size_t)n);
                        if (rc != 0) { result = rc; goto cleanup; }
                        bytes_left -= n;
                    }
                }

                moov_child_pos = child.offset + child.size;
            }

            /* Write new udta */
            rc = file_write(tmp, udta_buf->data, udta_buf->size);
            if (rc != 0) { result = rc; goto cleanup; }

        } else {
            /* Copy box as-is */
            rc = file_seek(ctx->fh, box.offset);
            if (rc != 0) { result = rc; goto cleanup; }

            int64_t bytes_left = box.size;
            uint8_t copy_buf[65536];
            while (bytes_left > 0) {
                size_t to_read = (size_t)(bytes_left < (int64_t)sizeof(copy_buf)
                                          ? bytes_left : (int64_t)sizeof(copy_buf));
                int64_t n = file_read_partial(ctx->fh, copy_buf, to_read);
                if (n <= 0) break;
                rc = file_write(tmp, copy_buf, (size_t)n);
                if (rc != 0) { result = rc; goto cleanup; }
                bytes_left -= n;
            }
        }

        pos = box.offset + box.size;
    }

    if (file_sync(tmp) != 0) { result = MP4TAG_ERR_IO; goto cleanup; }

    file_close(tmp); tmp = NULL;
    file_close(ctx->fh); ctx->fh = NULL;

    if (rename(tmp_path, ctx->path) != 0) {
        result = MP4TAG_ERR_RENAME_FAILED;
        ctx->fh = ctx->writable ? file_open_rw(ctx->path)
                                : file_open_read(ctx->path);
        goto cleanup_path;
    }

    /* Reopen the file */
    ctx->fh = ctx->writable ? file_open_rw(ctx->path)
                            : file_open_read(ctx->path);
    if (!ctx->fh) { result = MP4TAG_ERR_IO; goto cleanup_path; }

    /* Re-parse structure */
    mp4_parse_structure(ctx->fh, &ctx->info);

cleanup:
    if (tmp) { file_close(tmp); unlink(tmp_path); }
cleanup_path:
    free(tmp_path);
    return result;
}

/* ------------------------------------------------------------------ */
/*  Tag writing: main entry point                                      */
/* ------------------------------------------------------------------ */

int mp4tag_write_tags(mp4tag_context_t *ctx, const mp4tag_collection_t *tags)
{
    if (!ctx || !tags)   return MP4TAG_ERR_INVALID_ARG;
    if (!ctx->fh)        return MP4TAG_ERR_NOT_OPEN;
    if (!ctx->writable)  return MP4TAG_ERR_READ_ONLY;

    invalidate_cache(ctx);

    /* Serialize ilst content */
    dyn_buffer_t ilst_content;
    buffer_init(&ilst_content);
    int rc = mp4_tags_serialize_ilst(tags, &ilst_content);
    if (rc != MP4TAG_OK) { buffer_free(&ilst_content); return rc; }

    /* Strategy 1: try in-place if ilst already exists */
    if (ctx->info.has_ilst) {
        rc = try_inplace(ctx, &ilst_content);
        if (rc == MP4TAG_OK) {
            buffer_free(&ilst_content);
            return MP4TAG_OK;
        }
    }

    buffer_free(&ilst_content);

    /* Strategy 2: rewrite the file */
    dyn_buffer_t udta_buf;
    buffer_init(&udta_buf);
    rc = mp4_tags_build_udta(tags, &udta_buf);
    if (rc != MP4TAG_OK) { buffer_free(&udta_buf); return rc; }

    rc = rewrite_file(ctx, &udta_buf);
    buffer_free(&udta_buf);

    return rc;
}

/* ------------------------------------------------------------------ */
/*  Convenience: set / remove single tag                               */
/* ------------------------------------------------------------------ */

static mp4tag_simple_tag_t *clone_simple_tag(const mp4tag_simple_tag_t *src)
{
    mp4tag_simple_tag_t *st = calloc(1, sizeof(*st));
    if (!st) return NULL;

    st->name       = str_dup(src->name);
    st->value      = str_dup(src->value);
    st->language   = str_dup(src->language);
    st->is_default = src->is_default;

    if (src->binary && src->binary_size > 0) {
        st->binary = malloc(src->binary_size);
        if (st->binary) {
            memcpy(st->binary, src->binary, src->binary_size);
            st->binary_size = src->binary_size;
        }
    }

    return st;
}

int mp4tag_set_tag_string(mp4tag_context_t *ctx, const char *name,
                          const char *value)
{
    if (!ctx || !name)   return MP4TAG_ERR_INVALID_ARG;
    if (!ctx->fh)        return MP4TAG_ERR_NOT_OPEN;
    if (!ctx->writable)  return MP4TAG_ERR_READ_ONLY;

    mp4tag_collection_t *existing = NULL;
    mp4tag_read_tags(ctx, &existing);

    mp4tag_collection_t *work = calloc(1, sizeof(*work));
    if (!work) return MP4TAG_ERR_NO_MEMORY;

    mp4tag_tag_t *wtag = calloc(1, sizeof(*wtag));
    if (!wtag) { free(work); return MP4TAG_ERR_NO_MEMORY; }
    wtag->target_type = MP4TAG_TARGET_ALBUM;
    work->tags  = wtag;
    work->count = 1;

    /* Copy existing tags, skipping the one we're replacing */
    if (existing) {
        for (const mp4tag_tag_t *tag = existing->tags; tag; tag = tag->next) {
            for (const mp4tag_simple_tag_t *st = tag->simple_tags; st; st = st->next) {
                if (st->name && str_casecmp(st->name, name) == 0)
                    continue;

                mp4tag_simple_tag_t *copy = clone_simple_tag(st);
                if (copy) {
                    if (!wtag->simple_tags) {
                        wtag->simple_tags = copy;
                    } else {
                        mp4tag_simple_tag_t *tail = wtag->simple_tags;
                        while (tail->next) tail = tail->next;
                        tail->next = copy;
                    }
                }
            }
        }
    }

    /* Add the new/updated tag */
    if (value) {
        mp4tag_simple_tag_t *st = calloc(1, sizeof(*st));
        if (!st) { mp4_tags_free_collection(work); return MP4TAG_ERR_NO_MEMORY; }
        st->name  = str_dup(name);
        st->value = str_dup(value);

        if (!wtag->simple_tags) {
            wtag->simple_tags = st;
        } else {
            mp4tag_simple_tag_t *tail = wtag->simple_tags;
            while (tail->next) tail = tail->next;
            tail->next = st;
        }
    }

    int rc = mp4tag_write_tags(ctx, work);
    mp4_tags_free_collection(work);
    return rc;
}

int mp4tag_remove_tag(mp4tag_context_t *ctx, const char *name)
{
    return mp4tag_set_tag_string(ctx, name, NULL);
}

/* ------------------------------------------------------------------ */
/*  Collection building API                                            */
/* ------------------------------------------------------------------ */

mp4tag_collection_t *mp4tag_collection_create(mp4tag_context_t *ctx)
{
    (void)ctx;
    return calloc(1, sizeof(mp4tag_collection_t));
}

void mp4tag_collection_free(mp4tag_context_t *ctx, mp4tag_collection_t *coll)
{
    (void)ctx;
    mp4_tags_free_collection(coll);
}

mp4tag_tag_t *mp4tag_collection_add_tag(mp4tag_context_t *ctx,
                                        mp4tag_collection_t *coll,
                                        mp4tag_target_type_t type)
{
    (void)ctx;
    if (!coll) return NULL;

    mp4tag_tag_t *tag = calloc(1, sizeof(*tag));
    if (!tag) return NULL;
    tag->target_type = type;

    if (!coll->tags) {
        coll->tags = tag;
    } else {
        mp4tag_tag_t *tail = coll->tags;
        while (tail->next) tail = tail->next;
        tail->next = tag;
    }
    coll->count++;
    return tag;
}

mp4tag_simple_tag_t *mp4tag_tag_add_simple(mp4tag_context_t *ctx,
                                           mp4tag_tag_t *tag,
                                           const char *name,
                                           const char *value)
{
    (void)ctx;
    if (!tag || !name) return NULL;

    mp4tag_simple_tag_t *st = calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->name  = str_dup(name);
    st->value = value ? str_dup(value) : NULL;
    if (!st->name) { free(st); return NULL; }

    if (!tag->simple_tags) {
        tag->simple_tags = st;
    } else {
        mp4tag_simple_tag_t *tail = tag->simple_tags;
        while (tail->next) tail = tail->next;
        tail->next = st;
    }
    return st;
}

mp4tag_simple_tag_t *mp4tag_simple_tag_add_nested(mp4tag_context_t *ctx,
                                                  mp4tag_simple_tag_t *parent,
                                                  const char *name,
                                                  const char *value)
{
    (void)ctx;
    if (!parent || !name) return NULL;

    mp4tag_simple_tag_t *st = calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->name  = str_dup(name);
    st->value = value ? str_dup(value) : NULL;
    if (!st->name) { free(st); return NULL; }

    if (!parent->nested) {
        parent->nested = st;
    } else {
        mp4tag_simple_tag_t *tail = parent->nested;
        while (tail->next) tail = tail->next;
        tail->next = st;
    }
    return st;
}

int mp4tag_simple_tag_set_language(mp4tag_context_t *ctx,
                                  mp4tag_simple_tag_t *simple_tag,
                                  const char *language)
{
    (void)ctx;
    if (!simple_tag) return MP4TAG_ERR_INVALID_ARG;
    free(simple_tag->language);
    simple_tag->language = language ? str_dup(language) : NULL;
    return MP4TAG_OK;
}

int mp4tag_tag_add_track_uid(mp4tag_context_t *ctx, mp4tag_tag_t *tag,
                             uint64_t uid)
{
    (void)ctx;
    if (!tag) return MP4TAG_ERR_INVALID_ARG;

    uint64_t *new_uids = realloc(tag->track_uids,
                                  (tag->track_uid_count + 1) * sizeof(uint64_t));
    if (!new_uids) return MP4TAG_ERR_NO_MEMORY;

    new_uids[tag->track_uid_count] = uid;
    tag->track_uids = new_uids;
    tag->track_uid_count++;
    return MP4TAG_OK;
}

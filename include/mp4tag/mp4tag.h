/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef MP4TAG_H
#define MP4TAG_H

#include "mp4tag_types.h"
#include "mp4tag_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Version ---------- */

const char *mp4tag_version(void);

/* ---------- Error ---------- */

const char *mp4tag_strerror(int error);

/* ---------- Context lifecycle ---------- */

mp4tag_context_t *mp4tag_create(const mp4tag_allocator_t *allocator);
void              mp4tag_destroy(mp4tag_context_t *ctx);

int  mp4tag_open(mp4tag_context_t *ctx, const char *path);
int  mp4tag_open_rw(mp4tag_context_t *ctx, const char *path);
void mp4tag_close(mp4tag_context_t *ctx);
int  mp4tag_is_open(const mp4tag_context_t *ctx);

/* ---------- Tag reading ---------- */

/*
 * Read all tags from the file. The returned collection is owned by
 * the context and must not be freed by the caller. It remains valid
 * until the next call to mp4tag_read_tags, mp4tag_write_tags,
 * mp4tag_set_tag_string, mp4tag_remove_tag, or mp4tag_close.
 */
int mp4tag_read_tags(mp4tag_context_t *ctx, mp4tag_collection_t **tags);

/*
 * Read a single tag value by name (case-insensitive).
 * Copies the value into the caller-provided buffer.
 */
int mp4tag_read_tag_string(mp4tag_context_t *ctx, const char *name,
                           char *value, size_t size);

/* ---------- Tag writing ---------- */

/*
 * Replace all tags in the file with the given collection.
 * Attempts in-place write first; falls back to rewrite if needed.
 */
int mp4tag_write_tags(mp4tag_context_t *ctx, const mp4tag_collection_t *tags);

/*
 * Set or create a single tag. Pass NULL as value to remove the tag.
 * Tags are placed at the ALBUM target level (type 50).
 */
int mp4tag_set_tag_string(mp4tag_context_t *ctx, const char *name,
                          const char *value);

/*
 * Remove a tag by name.
 */
int mp4tag_remove_tag(mp4tag_context_t *ctx, const char *name);

/* ---------- Collection building ---------- */

mp4tag_collection_t *mp4tag_collection_create(mp4tag_context_t *ctx);
void                 mp4tag_collection_free(mp4tag_context_t *ctx,
                                            mp4tag_collection_t *coll);

mp4tag_tag_t *mp4tag_collection_add_tag(mp4tag_context_t *ctx,
                                        mp4tag_collection_t *coll,
                                        mp4tag_target_type_t type);

mp4tag_simple_tag_t *mp4tag_tag_add_simple(mp4tag_context_t *ctx,
                                           mp4tag_tag_t *tag,
                                           const char *name,
                                           const char *value);

mp4tag_simple_tag_t *mp4tag_simple_tag_add_nested(mp4tag_context_t *ctx,
                                                  mp4tag_simple_tag_t *parent,
                                                  const char *name,
                                                  const char *value);

int mp4tag_simple_tag_set_language(mp4tag_context_t *ctx,
                                  mp4tag_simple_tag_t *simple_tag,
                                  const char *language);

int mp4tag_tag_add_track_uid(mp4tag_context_t *ctx, mp4tag_tag_t *tag,
                             uint64_t uid);

#ifdef __cplusplus
}
#endif

#endif /* MP4TAG_H */

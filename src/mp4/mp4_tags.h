/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef MP4_TAGS_H
#define MP4_TAGS_H

#include "../../include/mp4tag/mp4tag_types.h"
#include <tag_common/file_io.h>
#include <tag_common/buffer.h>
#include "../util/mp4_buffer_ext.h"
#include "mp4_atoms.h"
#include "mp4_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse the ilst box and build a collection.
 * The caller must free the collection with mp4_tags_free_collection().
 */
int mp4_tags_parse_ilst(file_handle_t *fh, const mp4_file_info_t *info,
                        mp4tag_collection_t **out);

/*
 * Serialize a tag collection into an ilst box payload (not including
 * the ilst header itself).
 */
int mp4_tags_serialize_ilst(const mp4tag_collection_t *coll, dyn_buffer_t *buf);

/*
 * Build a complete moov > udta > meta > ilst hierarchy ready to write.
 * Includes hdlr box and the ilst content. Output is the udta box payload
 * (starting from udta header).
 */
int mp4_tags_build_udta(const mp4tag_collection_t *coll, dyn_buffer_t *buf);

/*
 * Free a tag collection and all its contents.
 */
void mp4_tags_free_collection(mp4tag_collection_t *coll);

/*
 * Map a human-readable tag name to an MP4 atom FourCC.
 * Returns 0 if no mapping exists (caller should use as-is or TXXX-style).
 */
uint32_t mp4_tag_name_to_fourcc(const char *name);

/*
 * Map an MP4 atom FourCC to a human-readable tag name.
 * Returns NULL if no mapping exists.
 */
const char *mp4_tag_fourcc_to_name(uint32_t fourcc);

#ifdef __cplusplus
}
#endif

#endif /* MP4_TAGS_H */

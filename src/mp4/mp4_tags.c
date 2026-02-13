/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "mp4_tags.h"
#include "../../include/mp4tag/mp4tag_error.h"
#include <tag_common/string_util.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Tag name <-> FourCC mapping table                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    uint32_t    fourcc;
} tag_mapping_t;

static const tag_mapping_t g_tag_map[] = {
    { "TITLE",             MP4_TAG_NAM  },
    { "ARTIST",            MP4_TAG_ART  },
    { "ALBUM",             MP4_TAG_ALB  },
    { "ALBUM_ARTIST",      MP4_TAG_AART },
    { "DATE_RELEASED",     MP4_TAG_DAY  },
    { "TRACK_NUMBER",      MP4_TAG_TRKN },
    { "DISC_NUMBER",       MP4_TAG_DISK },
    { "GENRE",             MP4_TAG_GEN  },
    { "COMPOSER",          MP4_TAG_WRT  },
    { "COMMENT",           MP4_TAG_CMT  },
    { "ENCODER",           MP4_TAG_TOO  },
    { "COPYRIGHT",         MP4_TAG_CPRT },
    { "BPM",               MP4_TAG_TMPO },
    { "LYRICS",            MP4_TAG_LYR  },
    { "GROUPING",          MP4_TAG_GRP  },
    { "DESCRIPTION",       MP4_TAG_DESC },
    { "COVER_ART",         MP4_TAG_COVR },
    { "COMPILATION",       MP4_TAG_CPIL },
    { "GAPLESS",           MP4_TAG_PGAP },
    { "SORT_NAME",         MP4_TAG_SONM },
    { "SORT_ARTIST",       MP4_TAG_SOAR },
    { "SORT_ALBUM",        MP4_TAG_SOAL },
    { "SORT_ALBUM_ARTIST", MP4_TAG_SOAA },
    { "SORT_COMPOSER",     MP4_TAG_SOCO },
    { NULL, 0 }
};

uint32_t mp4_tag_name_to_fourcc(const char *name)
{
    if (!name) return 0;

    for (const tag_mapping_t *m = g_tag_map; m->name; m++) {
        if (str_casecmp(name, m->name) == 0)
            return m->fourcc;
    }

    /* If exactly 4 chars, treat as raw FourCC */
    if (strlen(name) == 4)
        return mp4_str_to_fourcc(name);

    return 0;
}

const char *mp4_tag_fourcc_to_name(uint32_t fourcc)
{
    for (const tag_mapping_t *m = g_tag_map; m->name; m++) {
        if (m->fourcc == fourcc)
            return m->name;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Free helpers                                                       */
/* ------------------------------------------------------------------ */

static void free_simple_tags(mp4tag_simple_tag_t *st)
{
    while (st) {
        mp4tag_simple_tag_t *next = st->next;
        free(st->name);
        free(st->value);
        free(st->binary);
        free(st->language);
        free_simple_tags(st->nested);
        free(st);
        st = next;
    }
}

static void free_tag(mp4tag_tag_t *tag)
{
    while (tag) {
        mp4tag_tag_t *next = tag->next;
        free(tag->target_type_str);
        free(tag->track_uids);
        free(tag->edition_uids);
        free(tag->chapter_uids);
        free(tag->attachment_uids);
        free_simple_tags(tag->simple_tags);
        free(tag);
        tag = next;
    }
}

void mp4_tags_free_collection(mp4tag_collection_t *coll)
{
    if (!coll) return;
    free_tag(coll->tags);
    free(coll);
}

/* ------------------------------------------------------------------ */
/*  Parsing: ilst -> collection                                        */
/* ------------------------------------------------------------------ */

/*
 * Read a single ilst item atom and extract name + value.
 * Each ilst item is a box whose type is the tag key (e.g. ©nam).
 * Inside is a 'data' box with: 4-byte type indicator + 4-byte locale + data.
 */
static int parse_ilst_item(file_handle_t *fh, const mp4_box_t *item_box,
                           mp4tag_simple_tag_t **out)
{
    /* Look for the 'data' sub-box */
    int64_t pos = item_box->data_offset;
    int64_t end = item_box->offset + item_box->size;

    while (pos + 8 <= end) {
        int rc = file_seek(fh, pos);
        if (rc != 0) return rc;

        mp4_box_t child;
        rc = mp4_read_box_header(fh, &child);
        if (rc != 0) return rc;

        if (child.size < 8) break;

        if (child.type == MP4_BOX_DATA && child.data_size >= 8) {
            /* Read type indicator (4 bytes) + locale (4 bytes) */
            uint8_t hdr[8];
            rc = file_read(fh, hdr, 8);
            if (rc != 0) return rc;

            uint32_t data_type = ((uint32_t)hdr[0] << 24) |
                                 ((uint32_t)hdr[1] << 16) |
                                 ((uint32_t)hdr[2] << 8)  |
                                  (uint32_t)hdr[3];

            int64_t value_size = child.data_size - 8;

            mp4tag_simple_tag_t *st = calloc(1, sizeof(*st));
            if (!st) return MP4TAG_ERR_NO_MEMORY;

            /* Map the item FourCC to a name */
            const char *name = mp4_tag_fourcc_to_name(item_box->type);
            if (name) {
                st->name = str_dup(name);
            } else {
                char fourcc_str[5];
                mp4_fourcc_to_str(item_box->type, fourcc_str);
                st->name = str_dup(fourcc_str);
            }

            /*
             * Well-known integer atoms: handle regardless of data_type,
             * since some encoders use IMPLICIT (0) and others use
             * INTEGER (21) for the same atoms.
             */
            int is_int_atom = (item_box->type == MP4_TAG_TRKN ||
                               item_box->type == MP4_TAG_DISK ||
                               item_box->type == MP4_TAG_TMPO ||
                               item_box->type == MP4_TAG_CPIL ||
                               item_box->type == MP4_TAG_PGAP);

            if (is_int_atom && value_size > 0 && value_size <= 8) {
                uint8_t int_buf[8];
                rc = file_read(fh, int_buf, (size_t)value_size);
                if (rc != 0) { free(st->name); free(st); return rc; }

                if ((item_box->type == MP4_TAG_TRKN ||
                     item_box->type == MP4_TAG_DISK) && value_size >= 6) {
                    uint16_t num   = ((uint16_t)int_buf[2] << 8) | int_buf[3];
                    uint16_t total = ((uint16_t)int_buf[4] << 8) | int_buf[5];
                    char num_str[32];
                    if (total > 0)
                        snprintf(num_str, sizeof(num_str), "%u/%u", num, total);
                    else
                        snprintf(num_str, sizeof(num_str), "%u", num);
                    st->value = str_dup(num_str);
                } else if (item_box->type == MP4_TAG_TMPO && value_size == 2) {
                    uint16_t bpm = ((uint16_t)int_buf[0] << 8) | int_buf[1];
                    char bpm_str[16];
                    snprintf(bpm_str, sizeof(bpm_str), "%u", bpm);
                    st->value = str_dup(bpm_str);
                } else if (value_size == 1) {
                    char bool_str[4];
                    snprintf(bool_str, sizeof(bool_str), "%u", int_buf[0]);
                    st->value = str_dup(bool_str);
                } else {
                    uint64_t ival = 0;
                    for (int64_t i = 0; i < value_size; i++)
                        ival = (ival << 8) | int_buf[i];
                    char ival_str[32];
                    snprintf(ival_str, sizeof(ival_str), "%llu",
                             (unsigned long long)ival);
                    st->value = str_dup(ival_str);
                }
            } else if (data_type == MP4_DATA_UTF8 ||
                       data_type == MP4_DATA_IMPLICIT) {
                /* Text data */
                if (value_size > 0) {
                    char *text = malloc((size_t)value_size + 1);
                    if (!text) { free(st->name); free(st); return MP4TAG_ERR_NO_MEMORY; }
                    rc = file_read(fh, text, (size_t)value_size);
                    if (rc != 0) { free(text); free(st->name); free(st); return rc; }
                    text[value_size] = '\0';
                    st->value = text;
                }
            } else if (data_type == MP4_DATA_INTEGER) {
                /* Generic integer data */
                if (value_size > 0 && value_size <= 8) {
                    uint8_t int_buf[8];
                    rc = file_read(fh, int_buf, (size_t)value_size);
                    if (rc != 0) { free(st->name); free(st); return rc; }
                    uint64_t ival = 0;
                    for (int64_t i = 0; i < value_size; i++)
                        ival = (ival << 8) | int_buf[i];
                    char ival_str[32];
                    snprintf(ival_str, sizeof(ival_str), "%llu",
                             (unsigned long long)ival);
                    st->value = str_dup(ival_str);
                }
            } else if (data_type == MP4_DATA_JPEG || data_type == MP4_DATA_PNG) {
                /* Binary image data */
                if (value_size > 0) {
                    st->binary = malloc((size_t)value_size);
                    if (!st->binary) { free(st->name); free(st); return MP4TAG_ERR_NO_MEMORY; }
                    rc = file_read(fh, st->binary, (size_t)value_size);
                    if (rc != 0) {
                        free(st->binary); free(st->name); free(st);
                        return rc;
                    }
                    st->binary_size = (size_t)value_size;
                }
            } else {
                /* Other binary data */
                if (value_size > 0) {
                    st->binary = malloc((size_t)value_size);
                    if (!st->binary) { free(st->name); free(st); return MP4TAG_ERR_NO_MEMORY; }
                    rc = file_read(fh, st->binary, (size_t)value_size);
                    if (rc != 0) {
                        free(st->binary); free(st->name); free(st);
                        return rc;
                    }
                    st->binary_size = (size_t)value_size;
                }
            }

            st->is_default = 1;
            *out = st;
            return MP4TAG_OK;
        }

        pos = child.offset + child.size;
    }

    return MP4TAG_ERR_TAG_NOT_FOUND;
}

int mp4_tags_parse_ilst(file_handle_t *fh, const mp4_file_info_t *info,
                        mp4tag_collection_t **out)
{
    if (!fh || !info || !out) return MP4TAG_ERR_INVALID_ARG;
    if (!info->has_ilst) return MP4TAG_ERR_NO_TAGS;

    mp4tag_collection_t *coll = calloc(1, sizeof(*coll));
    if (!coll) return MP4TAG_ERR_NO_MEMORY;

    mp4tag_tag_t *tag = calloc(1, sizeof(*tag));
    if (!tag) { free(coll); return MP4TAG_ERR_NO_MEMORY; }
    tag->target_type = MP4TAG_TARGET_ALBUM;
    coll->tags  = tag;
    coll->count = 1;

    /* Iterate ilst children */
    int64_t pos = info->ilst_offset + 8;  /* Skip ilst header */
    int64_t end = info->ilst_offset + info->ilst_size;

    while (pos + 8 <= end) {
        int rc = file_seek(fh, pos);
        if (rc != 0) break;

        mp4_box_t item;
        rc = mp4_read_box_header(fh, &item);
        if (rc != 0) break;
        if (item.size < 8) break;

        mp4tag_simple_tag_t *st = NULL;
        rc = parse_ilst_item(fh, &item, &st);
        if (rc == MP4TAG_OK && st) {
            /* Append to tag's simple_tags list */
            if (!tag->simple_tags) {
                tag->simple_tags = st;
            } else {
                mp4tag_simple_tag_t *tail = tag->simple_tags;
                while (tail->next) tail = tail->next;
                tail->next = st;
            }
        }

        pos = item.offset + item.size;
    }

    *out = coll;
    return MP4TAG_OK;
}

/* ------------------------------------------------------------------ */
/*  Serialization: collection -> ilst bytes                            */
/* ------------------------------------------------------------------ */

/*
 * Serialize a single tag item into the buffer.
 * Format: item_box { data_box { type_indicator + locale + value } }
 */
static int serialize_tag_item(const mp4tag_simple_tag_t *st, dyn_buffer_t *buf)
{
    uint32_t fourcc = mp4_tag_name_to_fourcc(st->name);
    if (fourcc == 0) {
        /* Use as raw FourCC if exactly 4 chars, otherwise skip */
        if (st->name && strlen(st->name) == 4)
            fourcc = mp4_str_to_fourcc(st->name);
        else
            return 0;  /* Skip unknown tags */
    }

    dyn_buffer_t data_content;
    buffer_init(&data_content);

    /* Determine data type and build content */
    uint32_t data_type = MP4_DATA_UTF8;

    if (fourcc == MP4_TAG_TRKN || fourcc == MP4_TAG_DISK) {
        /* Integer pair: num/total -> 8-byte packed format */
        data_type = MP4_DATA_IMPLICIT;
        unsigned num = 0, total = 0;
        if (st->value) {
            if (sscanf(st->value, "%u/%u", &num, &total) < 1)
                sscanf(st->value, "%u", &num);
        }
        uint8_t pair[8] = { 0, 0,
                            (uint8_t)(num >> 8),   (uint8_t)num,
                            (uint8_t)(total >> 8),  (uint8_t)total,
                            0, 0 };
        buffer_append(&data_content, pair, 8);
    } else if (fourcc == MP4_TAG_TMPO) {
        /* BPM: 2-byte integer */
        data_type = MP4_DATA_INTEGER;
        unsigned bpm = 0;
        if (st->value) sscanf(st->value, "%u", &bpm);
        uint8_t bpm_bytes[2] = { (uint8_t)(bpm >> 8), (uint8_t)bpm };
        buffer_append(&data_content, bpm_bytes, 2);
    } else if (fourcc == MP4_TAG_CPIL || fourcc == MP4_TAG_PGAP) {
        /* Boolean: 1 byte */
        data_type = MP4_DATA_INTEGER;
        uint8_t val = 0;
        if (st->value) {
            unsigned v = 0;
            sscanf(st->value, "%u", &v);
            val = (uint8_t)(v ? 1 : 0);
        }
        buffer_append_byte(&data_content, val);
    } else if (fourcc == MP4_TAG_COVR) {
        /* Cover art: binary */
        if (st->binary && st->binary_size > 0) {
            /* Detect JPEG vs PNG */
            if (st->binary_size >= 4 &&
                st->binary[0] == 0x89 && st->binary[1] == 0x50)
                data_type = MP4_DATA_PNG;
            else
                data_type = MP4_DATA_JPEG;
            buffer_append(&data_content, st->binary, st->binary_size);
        } else {
            buffer_free(&data_content);
            return 0;  /* No image data */
        }
    } else if (fourcc == MP4_TAG_GNRE) {
        /* Genre number: 2-byte integer */
        data_type = MP4_DATA_IMPLICIT;
        unsigned genre = 0;
        if (st->value) sscanf(st->value, "%u", &genre);
        uint8_t genre_bytes[2] = { (uint8_t)(genre >> 8), (uint8_t)genre };
        buffer_append(&data_content, genre_bytes, 2);
    } else {
        /* UTF-8 text */
        data_type = MP4_DATA_UTF8;
        if (st->value)
            buffer_append(&data_content, st->value, strlen(st->value));
    }

    /* Build data box: header(8) + type_indicator(4) + locale(4) + content */
    uint32_t data_box_size = 8 + 4 + 4 + (uint32_t)data_content.size;

    /* Build item box: header(8) + data_box */
    uint32_t item_box_size = 8 + data_box_size;

    /* Write item box header */
    mp4_write_box_header(buf, fourcc, item_box_size);

    /* Write data box header */
    mp4_write_box_header(buf, MP4_BOX_DATA, data_box_size);

    /* Write type indicator (4 bytes) */
    buffer_append_be32(buf, data_type);

    /* Write locale (4 bytes, always 0) */
    buffer_append_be32(buf, 0);

    /* Write content */
    buffer_append(buf, data_content.data, data_content.size);

    buffer_free(&data_content);
    return 0;
}

int mp4_tags_serialize_ilst(const mp4tag_collection_t *coll, dyn_buffer_t *buf)
{
    if (!coll || !buf) return MP4TAG_ERR_INVALID_ARG;

    for (const mp4tag_tag_t *tag = coll->tags; tag; tag = tag->next) {
        for (const mp4tag_simple_tag_t *st = tag->simple_tags; st; st = st->next) {
            if (!st->name) continue;
            int rc = serialize_tag_item(st, buf);
            if (rc != 0) return rc;
        }
    }

    return MP4TAG_OK;
}

int mp4_tags_build_udta(const mp4tag_collection_t *coll, dyn_buffer_t *buf)
{
    if (!coll || !buf) return MP4TAG_ERR_INVALID_ARG;

    /* Serialize ilst content */
    dyn_buffer_t ilst_content;
    buffer_init(&ilst_content);
    int rc = mp4_tags_serialize_ilst(coll, &ilst_content);
    if (rc != 0) { buffer_free(&ilst_content); return rc; }

    /* Build ilst box */
    uint32_t ilst_size = 8 + (uint32_t)ilst_content.size;

    /*
     * Build hdlr box for meta:
     * hdlr box = header(8) + version/flags(4) + pre-defined(4) +
     *            handler_type(4) + reserved(12) + name(1) = 33 bytes
     */
    uint8_t hdlr_data[] = {
        0, 0, 0, 0,              /* version + flags */
        0, 0, 0, 0,              /* pre-defined */
        'm', 'd', 'i', 'r',     /* handler_type = 'mdir' */
        'a', 'p', 'p', 'l',     /* reserved (Apple uses 'appl' here) */
        0, 0, 0, 0,              /* reserved */
        0, 0, 0, 0,              /* reserved */
        0                        /* name (empty C string) */
    };
    uint32_t hdlr_size = 8 + (uint32_t)sizeof(hdlr_data);

    /* meta box = header(8) + version/flags(4) + hdlr + ilst */
    uint32_t meta_content_size = 4 + hdlr_size + ilst_size;
    uint32_t meta_size = 8 + meta_content_size;

    /* udta box = header(8) + meta */
    uint32_t udta_size = 8 + meta_size;

    /* Write udta header */
    mp4_write_box_header(buf, MP4_BOX_UDTA, udta_size);

    /* Write meta header */
    mp4_write_box_header(buf, MP4_BOX_META, meta_size);
    buffer_append_be32(buf, 0);  /* version + flags */

    /* Write hdlr */
    mp4_write_box_header(buf, MP4_BOX_HDLR, hdlr_size);
    buffer_append(buf, hdlr_data, sizeof(hdlr_data));

    /* Write ilst */
    mp4_write_box_header(buf, MP4_BOX_ILST, ilst_size);
    buffer_append(buf, ilst_content.data, ilst_content.size);

    buffer_free(&ilst_content);
    return MP4TAG_OK;
}

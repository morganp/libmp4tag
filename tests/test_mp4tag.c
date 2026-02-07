/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

/*
 * Tests for libmp4tag — read/write MP4 metadata tags.
 *
 * Creates minimal valid MP4 files in memory, then exercises the
 * read/write/set/remove API.
 */

#include <mp4tag/mp4tag.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS: %s\n", msg); g_pass++; } \
    else      { printf("  FAIL: %s\n", msg); g_fail++; } \
} while (0)

#define CHECK_RC(rc, msg) CHECK((rc) == MP4TAG_OK, msg)

/* ------------------------------------------------------------------ */
/*  Helpers: write big-endian values                                   */
/* ------------------------------------------------------------------ */

static void write_be32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {
        (uint8_t)(v >> 24), (uint8_t)(v >> 16),
        (uint8_t)(v >> 8),  (uint8_t)v
    };
    fwrite(b, 1, 4, f);
}

static void write_fourcc(FILE *f, const char *cc)
{
    fwrite(cc, 1, 4, f);
}

static void write_bytes(FILE *f, const void *data, size_t n)
{
    fwrite(data, 1, n, f);
}

/* ------------------------------------------------------------------ */
/*  Minimal MP4 file generator                                         */
/* ------------------------------------------------------------------ */

/*
 * Create a minimal valid MP4 file with:
 *   ftyp box (isom brand)
 *   moov box containing:
 *     mvhd box (minimal)
 *     udta box containing:
 *       meta box containing:
 *         hdlr box (mdir/appl handler)
 *         ilst box containing:
 *           ©nam (TITLE) = "Test Title"
 *           ©ART (ARTIST) = "Test Artist"
 *     free box (512 bytes of padding)
 *   mdat box (8 bytes, empty)
 */
static void create_mp4_with_tags(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;

    /* --- ftyp box --- */
    write_be32(f, 20);             /* size */
    write_fourcc(f, "ftyp");
    write_fourcc(f, "isom");       /* major brand */
    write_be32(f, 0x200);          /* minor version */
    write_fourcc(f, "isom");       /* compatible brand */

    /* --- Build ilst items first to know sizes --- */

    /* ©nam item: header(8) + data_box(8+8+10) = 34 */
    const char *title = "Test Title";
    size_t title_len = strlen(title);
    uint32_t nam_data_size = 8 + 4 + 4 + (uint32_t)title_len;   /* 26 */
    uint32_t nam_item_size = 8 + nam_data_size;                  /* 34 */

    /* ©ART item */
    const char *artist = "Test Artist";
    size_t artist_len = strlen(artist);
    uint32_t art_data_size = 8 + 4 + 4 + (uint32_t)artist_len;
    uint32_t art_item_size = 8 + art_data_size;

    uint32_t ilst_size = 8 + nam_item_size + art_item_size;

    /* hdlr box: header(8) + version/flags(4) + pre_defined(4) +
     *           handler_type(4) + reserved(12) + name(1) = 33 bytes */
    uint32_t hdlr_size = 8 + 25;   /* 33 */

    /* meta box: header(8) + version/flags(4) + hdlr + ilst */
    uint32_t meta_size = 8 + 4 + hdlr_size + ilst_size;

    /* free box inside moov after udta */
    uint32_t free_size = 512;

    /* udta box: header(8) + meta */
    uint32_t udta_size = 8 + meta_size;

    /* mvhd box: header(8) + version(1) + flags(3) + data(96) = 108 */
    uint32_t mvhd_size = 108;

    /* moov box */
    uint32_t moov_size = 8 + mvhd_size + udta_size + free_size;

    /* --- Write moov --- */
    write_be32(f, moov_size);
    write_fourcc(f, "moov");

    /* mvhd (version 0, minimal) */
    write_be32(f, mvhd_size);
    write_fourcc(f, "mvhd");
    /* version=0, flags=0, creation/modification time, timescale, duration */
    uint8_t mvhd_data[100];
    memset(mvhd_data, 0, sizeof(mvhd_data));
    /* timescale at offset 12 (from start of mvhd data) */
    mvhd_data[12] = 0; mvhd_data[13] = 0;
    mvhd_data[14] = 0x03; mvhd_data[15] = 0xE8;  /* timescale = 1000 */
    /* next_track_ID at offset 96 */
    mvhd_data[96] = 0; mvhd_data[97] = 0;
    mvhd_data[98] = 0; mvhd_data[99] = 1;         /* next_track = 1 */
    write_bytes(f, mvhd_data, sizeof(mvhd_data));

    /* udta */
    write_be32(f, udta_size);
    write_fourcc(f, "udta");

    /* meta (full box) */
    write_be32(f, meta_size);
    write_fourcc(f, "meta");
    write_be32(f, 0);  /* version + flags */

    /* hdlr */
    write_be32(f, hdlr_size);
    write_fourcc(f, "hdlr");
    write_be32(f, 0);              /* version + flags */
    write_be32(f, 0);              /* pre-defined */
    write_fourcc(f, "mdir");       /* handler type */
    write_fourcc(f, "appl");       /* reserved (Apple convention) */
    write_be32(f, 0);              /* reserved */
    write_be32(f, 0);              /* reserved */
    uint8_t zero = 0;
    write_bytes(f, &zero, 1);      /* name (empty string) */

    /* ilst */
    write_be32(f, ilst_size);
    write_fourcc(f, "ilst");

    /* ©nam item */
    write_be32(f, nam_item_size);
    {
        uint8_t nam_type[4] = { 0xA9, 'n', 'a', 'm' };
        write_bytes(f, nam_type, 4);
    }
    write_be32(f, nam_data_size);
    write_fourcc(f, "data");
    write_be32(f, 1);              /* data type = UTF-8 */
    write_be32(f, 0);              /* locale */
    write_bytes(f, title, title_len);

    /* ©ART item */
    write_be32(f, art_item_size);
    {
        uint8_t art_type[4] = { 0xA9, 'A', 'R', 'T' };
        write_bytes(f, art_type, 4);
    }
    write_be32(f, art_data_size);
    write_fourcc(f, "data");
    write_be32(f, 1);              /* data type = UTF-8 */
    write_be32(f, 0);              /* locale */
    write_bytes(f, artist, artist_len);

    /* free box (padding) */
    write_be32(f, free_size);
    write_fourcc(f, "free");
    {
        uint8_t pad[504];  /* 512 - 8 */
        memset(pad, 0, sizeof(pad));
        write_bytes(f, pad, sizeof(pad));
    }

    /* --- mdat (empty) --- */
    write_be32(f, 8);
    write_fourcc(f, "mdat");

    fclose(f);
}

/*
 * Create a minimal valid MP4 with no tags (no udta/meta/ilst).
 */
static void create_mp4_no_tags(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;

    /* ftyp */
    write_be32(f, 20);
    write_fourcc(f, "ftyp");
    write_fourcc(f, "M4A ");
    write_be32(f, 0);
    write_fourcc(f, "M4A ");

    /* moov with just mvhd */
    uint32_t mvhd_size = 108;
    uint32_t moov_size = 8 + mvhd_size;
    write_be32(f, moov_size);
    write_fourcc(f, "moov");

    write_be32(f, mvhd_size);
    write_fourcc(f, "mvhd");
    uint8_t mvhd_data[100];
    memset(mvhd_data, 0, sizeof(mvhd_data));
    mvhd_data[14] = 0x03; mvhd_data[15] = 0xE8;
    mvhd_data[98] = 0; mvhd_data[99] = 1;
    write_bytes(f, mvhd_data, sizeof(mvhd_data));

    /* mdat */
    write_be32(f, 8);
    write_fourcc(f, "mdat");

    fclose(f);
}

/*
 * Create a minimal MP4 with integer tags (trkn, disk, tmpo, cpil).
 */
static void create_mp4_with_int_tags(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;

    /* ftyp */
    write_be32(f, 20);
    write_fourcc(f, "ftyp");
    write_fourcc(f, "isom");
    write_be32(f, 0);
    write_fourcc(f, "isom");

    /* trkn item: header(8) + data(8+8+8) = 32 */
    uint32_t trkn_data_size = 8 + 4 + 4 + 8;   /* 24 */
    uint32_t trkn_item_size = 8 + trkn_data_size; /* 32 */

    /* tmpo item: header(8) + data(8+8+2) = 26 */
    uint32_t tmpo_data_size = 8 + 4 + 4 + 2;
    uint32_t tmpo_item_size = 8 + tmpo_data_size;

    /* cpil item: header(8) + data(8+8+1) = 25 */
    uint32_t cpil_data_size = 8 + 4 + 4 + 1;
    uint32_t cpil_item_size = 8 + cpil_data_size;

    uint32_t ilst_size = 8 + trkn_item_size + tmpo_item_size + cpil_item_size;
    uint32_t hdlr_size = 33;
    uint32_t meta_size = 8 + 4 + hdlr_size + ilst_size;
    uint32_t udta_size = 8 + meta_size;
    uint32_t mvhd_size = 108;
    uint32_t free_size = 256;
    uint32_t moov_size = 8 + mvhd_size + udta_size + free_size;

    /* moov */
    write_be32(f, moov_size);
    write_fourcc(f, "moov");

    /* mvhd */
    write_be32(f, mvhd_size);
    write_fourcc(f, "mvhd");
    uint8_t mvhd_data[100];
    memset(mvhd_data, 0, sizeof(mvhd_data));
    mvhd_data[14] = 0x03; mvhd_data[15] = 0xE8;
    mvhd_data[98] = 0; mvhd_data[99] = 1;
    write_bytes(f, mvhd_data, sizeof(mvhd_data));

    /* udta */
    write_be32(f, udta_size);
    write_fourcc(f, "udta");

    /* meta */
    write_be32(f, meta_size);
    write_fourcc(f, "meta");
    write_be32(f, 0);

    /* hdlr */
    write_be32(f, hdlr_size);
    write_fourcc(f, "hdlr");
    write_be32(f, 0); write_be32(f, 0);
    write_fourcc(f, "mdir"); write_fourcc(f, "appl");
    write_be32(f, 0); write_be32(f, 0);
    { uint8_t z = 0; write_bytes(f, &z, 1); }

    /* ilst */
    write_be32(f, ilst_size);
    write_fourcc(f, "ilst");

    /* trkn: track 3/12 */
    write_be32(f, trkn_item_size);
    write_fourcc(f, "trkn");
    write_be32(f, trkn_data_size);
    write_fourcc(f, "data");
    write_be32(f, 0);     /* implicit type */
    write_be32(f, 0);     /* locale */
    { uint8_t d[8] = { 0, 0, 0, 3, 0, 12, 0, 0 }; write_bytes(f, d, 8); }

    /* tmpo: 128 BPM */
    write_be32(f, tmpo_item_size);
    write_fourcc(f, "tmpo");
    write_be32(f, tmpo_data_size);
    write_fourcc(f, "data");
    write_be32(f, 21);    /* integer type */
    write_be32(f, 0);
    { uint8_t d[2] = { 0, 128 }; write_bytes(f, d, 2); }

    /* cpil: true */
    write_be32(f, cpil_item_size);
    write_fourcc(f, "cpil");
    write_be32(f, cpil_data_size);
    write_fourcc(f, "data");
    write_be32(f, 21);    /* integer type */
    write_be32(f, 0);
    { uint8_t d[1] = { 1 }; write_bytes(f, d, 1); }

    /* free */
    write_be32(f, free_size);
    write_fourcc(f, "free");
    { uint8_t pad[248]; memset(pad, 0, sizeof(pad)); write_bytes(f, pad, sizeof(pad)); }

    /* mdat */
    write_be32(f, 8);
    write_fourcc(f, "mdat");

    fclose(f);
}

/* ------------------------------------------------------------------ */
/*  Test suites                                                        */
/* ------------------------------------------------------------------ */

static void test_version(void)
{
    printf("\n--- Version ---\n");
    const char *v = mp4tag_version();
    CHECK(v != NULL && strlen(v) > 0, "mp4tag_version returns non-empty string");
}

static void test_error_strings(void)
{
    printf("\n--- Error strings ---\n");
    CHECK(strcmp(mp4tag_strerror(MP4TAG_OK), "Success") == 0,
          "strerror(OK) = Success");
    CHECK(strcmp(mp4tag_strerror(MP4TAG_ERR_NOT_MP4), "Not a supported MP4 file") == 0,
          "strerror(NOT_MP4)");
    CHECK(strlen(mp4tag_strerror(-999)) > 0,
          "strerror(unknown) returns non-empty");
}

static void test_context_lifecycle(void)
{
    printf("\n--- Context lifecycle ---\n");

    mp4tag_context_t *ctx = mp4tag_create(NULL);
    CHECK(ctx != NULL, "create returns non-NULL");
    CHECK(mp4tag_is_open(ctx) == 0, "not open initially");

    /* Destroy NULL is safe */
    mp4tag_destroy(NULL);
    CHECK(1, "destroy(NULL) does not crash");

    mp4tag_destroy(ctx);
    CHECK(1, "destroy works");
}

static void test_open_invalid(void)
{
    printf("\n--- Open invalid file ---\n");

    mp4tag_context_t *ctx = mp4tag_create(NULL);

    int rc = mp4tag_open(ctx, "/nonexistent/path.mp4");
    CHECK(rc != MP4TAG_OK, "open nonexistent file fails");

    /* Create a non-MP4 file */
    {
        FILE *f = fopen("/tmp/test_not_mp4.txt", "w");
        fprintf(f, "This is not an MP4 file.");
        fclose(f);
    }

    rc = mp4tag_open(ctx, "/tmp/test_not_mp4.txt");
    CHECK(rc == MP4TAG_ERR_NOT_MP4, "open non-MP4 returns NOT_MP4");

    mp4tag_destroy(ctx);
    remove("/tmp/test_not_mp4.txt");
}

static void test_read_tags(const char *path)
{
    printf("\n--- Read tags ---\n");

    mp4tag_context_t *ctx = mp4tag_create(NULL);
    int rc = mp4tag_open(ctx, path);
    CHECK_RC(rc, "open file with tags");
    CHECK(mp4tag_is_open(ctx) == 1, "file is open");

    /* Read all tags */
    mp4tag_collection_t *coll = NULL;
    rc = mp4tag_read_tags(ctx, &coll);
    CHECK_RC(rc, "read_tags succeeds");
    CHECK(coll != NULL, "collection is non-NULL");
    CHECK(coll->tags != NULL, "has tag entries");

    /* Read individual tags */
    char buf[256];
    rc = mp4tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
    CHECK_RC(rc, "read TITLE");
    CHECK(strcmp(buf, "Test Title") == 0, "TITLE = 'Test Title'");

    rc = mp4tag_read_tag_string(ctx, "ARTIST", buf, sizeof(buf));
    CHECK_RC(rc, "read ARTIST");
    CHECK(strcmp(buf, "Test Artist") == 0, "ARTIST = 'Test Artist'");

    /* Case-insensitive */
    rc = mp4tag_read_tag_string(ctx, "title", buf, sizeof(buf));
    CHECK_RC(rc, "read 'title' (case-insensitive)");
    CHECK(strcmp(buf, "Test Title") == 0, "case-insensitive TITLE match");

    /* Non-existent tag */
    rc = mp4tag_read_tag_string(ctx, "NONEXISTENT", buf, sizeof(buf));
    CHECK(rc == MP4TAG_ERR_TAG_NOT_FOUND, "non-existent tag returns TAG_NOT_FOUND");

    /* Buffer too small */
    rc = mp4tag_read_tag_string(ctx, "TITLE", buf, 4);
    CHECK(rc == MP4TAG_ERR_TAG_TOO_LARGE, "small buffer returns TAG_TOO_LARGE");

    mp4tag_destroy(ctx);
}

static void test_read_int_tags(const char *path)
{
    printf("\n--- Read integer tags ---\n");

    mp4tag_context_t *ctx = mp4tag_create(NULL);
    int rc = mp4tag_open(ctx, path);
    CHECK_RC(rc, "open file with int tags");

    char buf[256];

    rc = mp4tag_read_tag_string(ctx, "TRACK_NUMBER", buf, sizeof(buf));
    CHECK_RC(rc, "read TRACK_NUMBER");
    CHECK(strcmp(buf, "3/12") == 0, "TRACK_NUMBER = '3/12'");

    rc = mp4tag_read_tag_string(ctx, "BPM", buf, sizeof(buf));
    CHECK_RC(rc, "read BPM");
    CHECK(strcmp(buf, "128") == 0, "BPM = '128'");

    rc = mp4tag_read_tag_string(ctx, "COMPILATION", buf, sizeof(buf));
    CHECK_RC(rc, "read COMPILATION");
    CHECK(strcmp(buf, "1") == 0, "COMPILATION = '1'");

    mp4tag_destroy(ctx);
}

static void test_set_tag_string(const char *path)
{
    printf("\n--- Set tag string (in-place) ---\n");

    /* Copy the file first */
    const char *work_path = "/tmp/test_mp4tag_set.mp4";
    {
        FILE *src = fopen(path, "rb");
        FILE *dst = fopen(work_path, "wb");
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);
        fclose(src);
        fclose(dst);
    }

    mp4tag_context_t *ctx = mp4tag_create(NULL);
    int rc = mp4tag_open_rw(ctx, work_path);
    CHECK_RC(rc, "open_rw");

    /* Set a new value for TITLE (should fit in-place) */
    rc = mp4tag_set_tag_string(ctx, "TITLE", "New Title");
    CHECK_RC(rc, "set TITLE in-place");

    /* Verify */
    char buf[256];
    rc = mp4tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
    CHECK_RC(rc, "re-read TITLE");
    CHECK(strcmp(buf, "New Title") == 0, "TITLE updated to 'New Title'");

    /* ARTIST should still be there */
    rc = mp4tag_read_tag_string(ctx, "ARTIST", buf, sizeof(buf));
    CHECK_RC(rc, "ARTIST still present");
    CHECK(strcmp(buf, "Test Artist") == 0, "ARTIST unchanged");

    mp4tag_destroy(ctx);
    remove(work_path);
}

static void test_add_new_tag(const char *path)
{
    printf("\n--- Add new tag ---\n");

    const char *work_path = "/tmp/test_mp4tag_add.mp4";
    {
        FILE *src = fopen(path, "rb");
        FILE *dst = fopen(work_path, "wb");
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);
        fclose(src);
        fclose(dst);
    }

    mp4tag_context_t *ctx = mp4tag_create(NULL);
    int rc = mp4tag_open_rw(ctx, work_path);
    CHECK_RC(rc, "open_rw");

    rc = mp4tag_set_tag_string(ctx, "ALBUM", "Test Album");
    CHECK_RC(rc, "set ALBUM (new tag)");

    char buf[256];
    rc = mp4tag_read_tag_string(ctx, "ALBUM", buf, sizeof(buf));
    CHECK_RC(rc, "read ALBUM");
    CHECK(strcmp(buf, "Test Album") == 0, "ALBUM = 'Test Album'");

    /* Original tags preserved */
    rc = mp4tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
    CHECK_RC(rc, "TITLE still present");
    rc = mp4tag_read_tag_string(ctx, "ARTIST", buf, sizeof(buf));
    CHECK_RC(rc, "ARTIST still present");

    mp4tag_destroy(ctx);
    remove(work_path);
}

static void test_remove_tag(const char *path)
{
    printf("\n--- Remove tag ---\n");

    const char *work_path = "/tmp/test_mp4tag_remove.mp4";
    {
        FILE *src = fopen(path, "rb");
        FILE *dst = fopen(work_path, "wb");
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);
        fclose(src);
        fclose(dst);
    }

    mp4tag_context_t *ctx = mp4tag_create(NULL);
    int rc = mp4tag_open_rw(ctx, work_path);
    CHECK_RC(rc, "open_rw");

    rc = mp4tag_remove_tag(ctx, "ARTIST");
    CHECK_RC(rc, "remove ARTIST");

    char buf[256];
    rc = mp4tag_read_tag_string(ctx, "ARTIST", buf, sizeof(buf));
    CHECK(rc == MP4TAG_ERR_TAG_NOT_FOUND, "ARTIST removed");

    /* TITLE still there */
    rc = mp4tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
    CHECK_RC(rc, "TITLE still present after remove");

    mp4tag_destroy(ctx);
    remove(work_path);
}

static void test_write_no_existing_tags(const char *path)
{
    printf("\n--- Write to file with no tags (rewrite) ---\n");

    const char *work_path = "/tmp/test_mp4tag_notags.mp4";
    {
        FILE *src = fopen(path, "rb");
        FILE *dst = fopen(work_path, "wb");
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);
        fclose(src);
        fclose(dst);
    }

    mp4tag_context_t *ctx = mp4tag_create(NULL);
    int rc = mp4tag_open_rw(ctx, work_path);
    CHECK_RC(rc, "open_rw no-tag file");

    rc = mp4tag_set_tag_string(ctx, "TITLE", "Brand New Title");
    CHECK_RC(rc, "set TITLE on tagless file");

    char buf[256];
    rc = mp4tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
    CHECK_RC(rc, "read TITLE after write");
    CHECK(strcmp(buf, "Brand New Title") == 0, "TITLE = 'Brand New Title'");

    mp4tag_destroy(ctx);
    remove(work_path);
}

static void test_collection_api(void)
{
    printf("\n--- Collection building API ---\n");

    mp4tag_context_t *ctx = mp4tag_create(NULL);

    mp4tag_collection_t *coll = mp4tag_collection_create(ctx);
    CHECK(coll != NULL, "collection_create");

    mp4tag_tag_t *tag = mp4tag_collection_add_tag(ctx, coll, MP4TAG_TARGET_ALBUM);
    CHECK(tag != NULL, "collection_add_tag");

    mp4tag_simple_tag_t *st = mp4tag_tag_add_simple(ctx, tag, "TITLE", "My Song");
    CHECK(st != NULL, "tag_add_simple TITLE");
    CHECK(strcmp(st->name, "TITLE") == 0, "simple tag name");
    CHECK(strcmp(st->value, "My Song") == 0, "simple tag value");

    st = mp4tag_tag_add_simple(ctx, tag, "ARTIST", "Artist");
    CHECK(st != NULL, "tag_add_simple ARTIST");

    int rc = mp4tag_simple_tag_set_language(ctx, st, "eng");
    CHECK_RC(rc, "set_language");
    CHECK(strcmp(st->language, "eng") == 0, "language = eng");

    mp4tag_simple_tag_t *nested = mp4tag_simple_tag_add_nested(ctx, st, "CHILD", "val");
    CHECK(nested != NULL, "add_nested");

    rc = mp4tag_tag_add_track_uid(ctx, tag, 42);
    CHECK_RC(rc, "add_track_uid");
    CHECK(tag->track_uid_count == 1, "track_uid_count = 1");
    CHECK(tag->track_uids[0] == 42, "track_uid = 42");

    mp4tag_collection_free(ctx, coll);
    CHECK(1, "collection_free");

    mp4tag_destroy(ctx);
}

static void test_write_collection(const char *path)
{
    printf("\n--- Write full collection ---\n");

    const char *work_path = "/tmp/test_mp4tag_coll.mp4";
    {
        FILE *src = fopen(path, "rb");
        FILE *dst = fopen(work_path, "wb");
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);
        fclose(src);
        fclose(dst);
    }

    mp4tag_context_t *ctx = mp4tag_create(NULL);
    int rc = mp4tag_open_rw(ctx, work_path);
    CHECK_RC(rc, "open_rw for collection write");

    mp4tag_collection_t *coll = mp4tag_collection_create(ctx);
    mp4tag_tag_t *tag = mp4tag_collection_add_tag(ctx, coll, MP4TAG_TARGET_ALBUM);
    mp4tag_tag_add_simple(ctx, tag, "TITLE", "Collection Title");
    mp4tag_tag_add_simple(ctx, tag, "ARTIST", "Collection Artist");
    mp4tag_tag_add_simple(ctx, tag, "ALBUM", "Collection Album");
    mp4tag_tag_add_simple(ctx, tag, "DATE_RELEASED", "2025");

    rc = mp4tag_write_tags(ctx, coll);
    CHECK_RC(rc, "write_tags with collection");
    mp4tag_collection_free(ctx, coll);

    /* Verify */
    char buf[256];
    rc = mp4tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
    CHECK_RC(rc, "read TITLE from collection");
    CHECK(strcmp(buf, "Collection Title") == 0, "TITLE matches");

    rc = mp4tag_read_tag_string(ctx, "ALBUM", buf, sizeof(buf));
    CHECK_RC(rc, "read ALBUM from collection");
    CHECK(strcmp(buf, "Collection Album") == 0, "ALBUM matches");

    rc = mp4tag_read_tag_string(ctx, "DATE_RELEASED", buf, sizeof(buf));
    CHECK_RC(rc, "read DATE_RELEASED from collection");
    CHECK(strcmp(buf, "2025") == 0, "DATE_RELEASED matches");

    mp4tag_destroy(ctx);
    remove(work_path);
}

static void test_read_only_protection(const char *path)
{
    printf("\n--- Read-only protection ---\n");

    mp4tag_context_t *ctx = mp4tag_create(NULL);
    int rc = mp4tag_open(ctx, path);
    CHECK_RC(rc, "open read-only");

    rc = mp4tag_set_tag_string(ctx, "TITLE", "Should Fail");
    CHECK(rc == MP4TAG_ERR_READ_ONLY, "set_tag on read-only returns READ_ONLY");

    mp4tag_destroy(ctx);
}

static void test_reopen_after_write(const char *path)
{
    printf("\n--- Re-open and verify after write ---\n");

    const char *work_path = "/tmp/test_mp4tag_reopen.mp4";
    {
        FILE *src = fopen(path, "rb");
        FILE *dst = fopen(work_path, "wb");
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);
        fclose(src);
        fclose(dst);
    }

    /* Write tags */
    {
        mp4tag_context_t *ctx = mp4tag_create(NULL);
        mp4tag_open_rw(ctx, work_path);
        mp4tag_set_tag_string(ctx, "TITLE", "Persistent Title");
        mp4tag_set_tag_string(ctx, "ALBUM", "Persistent Album");
        mp4tag_destroy(ctx);
    }

    /* Re-open and verify */
    {
        mp4tag_context_t *ctx = mp4tag_create(NULL);
        int rc = mp4tag_open(ctx, work_path);
        CHECK_RC(rc, "re-open after write");

        char buf[256];
        rc = mp4tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
        CHECK_RC(rc, "read TITLE after re-open");
        CHECK(strcmp(buf, "Persistent Title") == 0, "TITLE persisted");

        rc = mp4tag_read_tag_string(ctx, "ALBUM", buf, sizeof(buf));
        CHECK_RC(rc, "read ALBUM after re-open");
        CHECK(strcmp(buf, "Persistent Album") == 0, "ALBUM persisted");

        mp4tag_destroy(ctx);
    }

    remove(work_path);
}

static void test_m4a_brand(void)
{
    printf("\n--- M4A brand detection ---\n");

    const char *path = "/tmp/test_m4a_brand.m4a";

    /* Create with M4A brand */
    FILE *f = fopen(path, "wb");
    write_be32(f, 20);
    write_fourcc(f, "ftyp");
    write_fourcc(f, "M4A ");
    write_be32(f, 0);
    write_fourcc(f, "M4A ");

    uint32_t mvhd_size = 108;
    uint32_t moov_size = 8 + mvhd_size;
    write_be32(f, moov_size);
    write_fourcc(f, "moov");
    write_be32(f, mvhd_size);
    write_fourcc(f, "mvhd");
    uint8_t mvhd[100];
    memset(mvhd, 0, sizeof(mvhd));
    mvhd[14] = 0x03; mvhd[15] = 0xE8;
    mvhd[98] = 0; mvhd[99] = 1;
    write_bytes(f, mvhd, sizeof(mvhd));

    write_be32(f, 8);
    write_fourcc(f, "mdat");
    fclose(f);

    mp4tag_context_t *ctx = mp4tag_create(NULL);
    int rc = mp4tag_open(ctx, path);
    CHECK_RC(rc, "open M4A file");
    CHECK(mp4tag_is_open(ctx) == 1, "M4A file is open");

    mp4tag_destroy(ctx);
    remove(path);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== libmp4tag test suite ===\n");

    const char *tagged_path  = "/tmp/test_mp4_tagged.mp4";
    const char *notag_path   = "/tmp/test_mp4_notag.mp4";
    const char *inttag_path  = "/tmp/test_mp4_inttags.mp4";

    /* Generate test files */
    create_mp4_with_tags(tagged_path);
    create_mp4_no_tags(notag_path);
    create_mp4_with_int_tags(inttag_path);

    /* Run tests */
    test_version();
    test_error_strings();
    test_context_lifecycle();
    test_open_invalid();
    test_read_tags(tagged_path);
    test_read_int_tags(inttag_path);
    test_set_tag_string(tagged_path);
    test_add_new_tag(tagged_path);
    test_remove_tag(tagged_path);
    test_write_no_existing_tags(notag_path);
    test_collection_api();
    test_write_collection(tagged_path);
    test_read_only_protection(tagged_path);
    test_reopen_after_write(tagged_path);
    test_m4a_brand();

    /* Cleanup */
    remove(tagged_path);
    remove(notag_path);
    remove(inttag_path);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

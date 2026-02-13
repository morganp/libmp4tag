# libmp4tag

A pure C library for reading and writing MP4/M4A/M4V/M4B metadata tags (iTunes-style) without loading entire files into memory.

API-compatible with [libmkvtag](https://github.com/morganp/libmkvtag) and [libmp3tag](https://github.com/morganp/libmp3tag) — swap `mkvtag_`/`mp3tag_` for `mp4tag_` and the same patterns apply.

## Features

- **Multi-format**: MP4, M4A, M4V, M4B, M4P — same API for all
- **Memory-efficient**: buffered I/O with 8KB read buffer; never loads the full audio/video into memory
- **In-place editing**: when the new tags fit within the existing ilst + adjacent free space, the file is updated in place without rewriting
- **Safe rewrite**: when more space is needed, writes to a temp file then performs an atomic rename
- **iTunes-compatible**: reads and writes the standard `moov > udta > meta > ilst` atom hierarchy with proper `hdlr` and `data` boxes
- **Integer tag support**: track/disc numbers (packed pair format), BPM, compilation flag — all read/written in native MP4 format
- **Cover art support**: reads and writes JPEG/PNG cover art via the `covr` atom
- **No dependencies**: only requires POSIX + C11 stdlib
- **Clean builds**: compiles with `-Wall -Wextra -Wpedantic`

## Quick Start

### Simple: read/write a single tag

```c
#include <mp4tag/mp4tag.h>
#include <stdio.h>

int main(void) {
    mp4tag_context_t *ctx = mp4tag_create(NULL);

    /* Read a tag */
    mp4tag_open(ctx, "song.m4a");
    char title[256];
    if (mp4tag_read_tag_string(ctx, "TITLE", title, sizeof(title)) == MP4TAG_OK)
        printf("Title: %s\n", title);
    mp4tag_close(ctx);

    /* Write a tag */
    mp4tag_open_rw(ctx, "song.m4a");
    mp4tag_set_tag_string(ctx, "TITLE", "New Title");
    mp4tag_set_tag_string(ctx, "ARTIST", "New Artist");
    mp4tag_close(ctx);

    mp4tag_destroy(ctx);
    return 0;
}
```

### Full control: build a tag collection

```c
mp4tag_context_t *ctx = mp4tag_create(NULL);
mp4tag_open_rw(ctx, "song.m4a");

mp4tag_collection_t *coll = mp4tag_collection_create(ctx);
mp4tag_tag_t *tag = mp4tag_collection_add_tag(ctx, coll, MP4TAG_TARGET_ALBUM);

mp4tag_tag_add_simple(ctx, tag, "TITLE",        "Song Title");
mp4tag_tag_add_simple(ctx, tag, "ARTIST",       "Artist Name");
mp4tag_tag_add_simple(ctx, tag, "ALBUM",        "Album Name");
mp4tag_tag_add_simple(ctx, tag, "TRACK_NUMBER", "3/12");
mp4tag_tag_add_simple(ctx, tag, "DATE_RELEASED","2025");

mp4tag_simple_tag_t *comment = mp4tag_tag_add_simple(ctx, tag, "COMMENT", "A comment");
mp4tag_simple_tag_set_language(ctx, comment, "eng");

mp4tag_write_tags(ctx, coll);
mp4tag_collection_free(ctx, coll);
mp4tag_close(ctx);
mp4tag_destroy(ctx);
```

## Dependencies

- [libtag_common](https://github.com/morganp/libtag_common) — shared I/O, buffer, and string utilities (included as git submodule)

## Building

Clone with submodules:

```bash
git clone --recursive https://github.com/morganp/libmp4tag.git
```

If already cloned:

```bash
git submodule update --init
```

### XCFramework (macOS + iOS)

```bash
./build_xcframework.sh
# Output: output/mp4tag.xcframework
```

### Manual build

```bash
xcrun clang -std=c11 -O2 -Wall -Iinclude -Isrc -Ideps/libtag_common/include -c src/*.c src/**/*.c
ar rcs libmp4tag.a *.o
```

## Supported Platforms

| Platform       | Architectures     | Min version |
|----------------|-------------------|-------------|
| macOS          | arm64, x86_64     | 10.15       |
| iOS            | arm64             | 13.0        |
| iOS Simulator  | arm64, x86_64     | 13.0        |

## API Reference

### Version & Error

| Function | Description |
|----------|-------------|
| `mp4tag_version()` | Returns version string ("1.0.0") |
| `mp4tag_strerror(int error)` | Human-readable error message |

### Context Lifecycle

| Function | Description |
|----------|-------------|
| `mp4tag_create(allocator)` | Create context (NULL = default malloc) |
| `mp4tag_destroy(ctx)` | Destroy context, close file |
| `mp4tag_open(ctx, path)` | Open file for reading |
| `mp4tag_open_rw(ctx, path)` | Open file for read/write |
| `mp4tag_close(ctx)` | Close file |
| `mp4tag_is_open(ctx)` | Check if a file is open |

### Tag Reading

| Function | Description |
|----------|-------------|
| `mp4tag_read_tags(ctx, &tags)` | Read all tags (context-owned) |
| `mp4tag_read_tag_string(ctx, name, buf, size)` | Read single tag by name |

### Tag Writing

| Function | Description |
|----------|-------------|
| `mp4tag_write_tags(ctx, tags)` | Replace all tags |
| `mp4tag_set_tag_string(ctx, name, value)` | Set/create single tag |
| `mp4tag_remove_tag(ctx, name)` | Remove a tag by name |

### Collection Building

| Function | Description |
|----------|-------------|
| `mp4tag_collection_create(ctx)` | Create empty collection |
| `mp4tag_collection_free(ctx, coll)` | Free collection |
| `mp4tag_collection_add_tag(ctx, coll, type)` | Add tag with target type |
| `mp4tag_tag_add_simple(ctx, tag, name, value)` | Add name/value pair |
| `mp4tag_simple_tag_add_nested(ctx, parent, name, value)` | Add nested child |
| `mp4tag_simple_tag_set_language(ctx, st, lang)` | Set language code |
| `mp4tag_tag_add_track_uid(ctx, tag, uid)` | Add track UID |

### Tag Name Mapping

| Name | MP4 Atom | Description |
|------|----------|-------------|
| TITLE | \©nam | Track title |
| ARTIST | \©ART | Lead artist |
| ALBUM | \©alb | Album name |
| ALBUM_ARTIST | aART | Album artist |
| DATE_RELEASED | \©day | Release date/year |
| TRACK_NUMBER | trkn | Track number (e.g. "3/12") |
| DISC_NUMBER | disk | Disc number (e.g. "1/2") |
| GENRE | \©gen | Genre (text) |
| COMPOSER | \©wrt | Composer |
| COMMENT | \©cmt | Comment |
| ENCODER | \©too | Encoding software |
| COPYRIGHT | cprt | Copyright |
| BPM | tmpo | Beats per minute |
| LYRICS | \©lyr | Lyrics |
| GROUPING | \©grp | Grouping |
| DESCRIPTION | desc | Description |
| COVER_ART | covr | Cover art (JPEG/PNG binary) |
| COMPILATION | cpil | Compilation flag (0/1) |
| GAPLESS | pgap | Gapless playback flag |
| SORT_NAME | sonm | Sort title |
| SORT_ARTIST | soar | Sort artist |
| SORT_ALBUM | soal | Sort album |
| SORT_ALBUM_ARTIST | soaa | Sort album artist |
| SORT_COMPOSER | soco | Sort composer |

Unknown tag names with exactly 4 characters are used as raw FourCC atom types.

## Supported File Types

| Extension | Brand | Notes |
|-----------|-------|-------|
| .mp4 | isom, mp41, mp42 | MPEG-4 video |
| .m4a | M4A | AAC audio (iTunes) |
| .m4b | M4B | Audiobook (iTunes) |
| .m4v | M4V | Video (iTunes) |
| .m4p | M4P | Protected AAC |
| .mov | qt | QuickTime |

The API is identical for all formats — the library auto-detects the container brand on open.

## Write Strategy

1. **In-place**: If the new tags fit within the existing `ilst` + adjacent `free` space, the file is updated in place with zero data copying
2. **Rewrite**: If more space is needed, the file is copied box-by-box to a temp file with the new moov/udta/meta/ilst structure, then atomically renamed

## Project Structure

```
libmp4tag/
├── include/mp4tag/         # Public headers
│   ├── mp4tag.h            # Main API
│   ├── mp4tag_types.h      # Type definitions
│   ├── mp4tag_error.h      # Error codes
│   └── module.modulemap    # Swift/Clang module map
├── deps/
│   └── libtag_common/      # Shared I/O, buffer & string utilities (submodule)
├── src/
│   ├── mp4tag.c            # Main API implementation
│   ├── mp4/                # MP4 format layer
│   │   ├── mp4_atoms.c     # Box header read/write, FourCC helpers
│   │   ├── mp4_parser.c    # File structure parsing (moov/udta/meta/ilst)
│   │   └── mp4_tags.c      # Tag parsing (ilst) and serialization
│   └── util/
│       └── mp4_buffer_ext.h # MP4-specific buffer extensions
├── tests/
│   └── test_mp4tag.c       # Test suite
└── build_xcframework.sh
```

## License

MIT

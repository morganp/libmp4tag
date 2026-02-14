# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Build library:
```sh
mkdir -p build && cd build && xcrun clang -c -std=c11 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2 -I ../include -I ../src -I ../deps/libtag_common/include \
    ../src/mp4tag.c ../src/mp4/mp4_atoms.c ../src/mp4/mp4_parser.c ../src/mp4/mp4_tags.c \
    ../deps/libtag_common/src/file_io.c ../deps/libtag_common/src/buffer.c ../deps/libtag_common/src/string_util.c \
    && xcrun ar rcs libmp4tag.a mp4tag.o mp4_atoms.o mp4_parser.o mp4_tags.o file_io.o buffer.o string_util.o
```

Build XCFramework (macOS + iOS):
```sh
./build_xcframework.sh
```

## Architecture

Pure C11 static library for reading/writing iTunes-style MP4/M4A metadata tags. No external dependencies (POSIX only). API is compatible with [libmkvtag](https://github.com/morganp/libmkvtag) and [libmp3tag](https://github.com/morganp/libmp3tag).

### Layers

- **Public API** (`include/mp4tag/`) — `mp4tag.h` (functions), `mp4tag_types.h` (structs/enums), `mp4tag_error.h` (error codes), `module.modulemap` (Swift/Clang)
- **Main implementation** (`src/mp4tag.c`) — Context lifecycle, tag read/write orchestration, collection building
- **MP4** (`src/mp4/`) — Box header read/write and FourCC helpers (`mp4_atoms`), file structure parsing for moov/udta/meta/ilst (`mp4_parser`), tag parsing and serialization (`mp4_tags`)
- **Util** (`src/util/`) — `mp4_buffer_ext.h` (MP4-specific buffer write helpers for big-endian integers)
- **Shared utilities** (`deps/libtag_common/`) — Buffered file I/O, dynamic byte buffer, string helpers (via libtag_common submodule)

### Write Strategy

- **In-place**: When new tags fit within existing `ilst` + adjacent `free` space, updates in place with zero data copying
- **Rewrite**: When more space is needed, copies box-by-box to temp file with new moov/udta/meta/ilst structure, then atomic rename

### Tag Name Mapping

Canonical names map to iTunes atoms: `TITLE`→`©nam`, `ARTIST`→`©ART`, `ALBUM`→`©alb`, `ALBUM_ARTIST`→`aART`, `DATE_RELEASED`→`©day`, `TRACK_NUMBER`→`trkn`, `DISC_NUMBER`→`disk`, `GENRE`→`©gen`, `COMMENT`→`©cmt`, `COVER_ART`→`covr`. Unknown 4-char names used as raw FourCC atom types.

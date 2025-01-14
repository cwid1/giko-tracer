# Giko Tracer

Giko Tracer is an open source project to convert images to ASCII and
ASCII-adjacent art. "Giko" is the name of a cat whose chariacature is drawn in
Shift_JIS (an ASCII-adjacent encoding) art.

> From henceforth, all ASCII and ASCII-adjacent art will be referred to
> collectively as "ascii art" (not capitalised).

## Key Features
- Ascii art generated with contour tracing
    - This allows for more compact and detailed ascii art.
- Ascii art can use any character from the unicode codespace
- Font agnostic
- Supports proportional fonts
- Fast and lightweight

## Components
- libgiko: a C API library
- giko-cli: a command line tool to convert bitmaps to ascii art

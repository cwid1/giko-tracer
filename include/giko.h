#ifndef GIKO__H
#define GIKO__H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Default arguments
#define DEFAULT_GLYPH_SIZE 16
#define DEFAUKT_CHUNK_GREED 0.5
#define DEFAULT_GLYPH_GREED 0.8

// Types
typedef struct giko_bitmap {
    int width;          // Width of image i.e. number of pixels across

    int pitch;          // Number of bytes per row. Aligned on 32 bit
                        // boundaries. If the width of an image is not divisible
                        // by 32, the row is padded with unset (0) bits.

    int height;         // Height of image.

    int real_size;      // Number of pixels in image i.e. width * height

    int buffer_size;    // Number of bits in bitmap i.e. pitch * height

    uint8_t *data;      // One-dimensional array of 8 bit bytes. Each bit
                        // represents either a set (1) pixel of an unset (0)
                        // pixel.
                        // The lowest memory address stores the 8 top-left
                        // pixels of the bitmap.
                        // The highest memory address is the 8 bottom-right
                        // pixels of the bitmap.
                        // The leftmost bit represents the leftmost pixel of the
                        // 8 pixels represented in a byte.

    int set_pixels;     // Number of set bits in the data field.
} giko_bitmap_t;

typedef struct giko_glyph_map giko_glyph_map_t;

typedef uint32_t giko_codepoint_t;

typedef enum { NONE, ASCENDING, DESCENDING } sort_order_t;

// Main functions

/*
 Generates a new bitmap from an array of pixel data.

Input:
    int width:  Width of the image (in number of pixels).

    int height: Height of the image (in number of pixels).

    int data:   A 1-dimensional array containing the pixel data of the bitmap.
                See giko_bitmap_t for more information on the format pixel data.

Output:
    - Returns a pointer to a giko_bitmap_t structure.
    - NULL if an error is encountered. Errors printed to stderr.
 */

giko_bitmap_t *giko_new_bitmap(int width, int height, uint8_t *data);

/*
 Generates a new glyph map from a .ttf (TrueType Fontface)

Input:
    char *ttf_filepath:         String representing path to the target fontface.

    giko_codepoint_t *charset:  Array of giko_codepoint_t terminated with 0.

    int glyph_size:             Target height (in pixels) of the font's
                                glyphs.

    enum sort_order:            Order to sort the glyphs when adding to
                                the mapping.
                                ASECNDING sorts the glyphs by least to most
                                number of set pixels.
                                DESCENDING sorts the glyphs by most to least
                                number of set pixels.
                                NONE adds glyphs to the map without sorting.

Output:
    - Returns a giko_glyph_map_t.
    - Returns NULL if an error is encountered. Errors printed to stderr.
 */
giko_glyph_map_t *giko_new_glyph_map(char *ttf_filepath,
                                     giko_codepoint_t *charset, int glyph_size,
                                     sort_order_t order);

/*
 Generates an ascii_art string from a reference bitmap and a glyph map.

Input:
    giko_bitmap_t *reference:   Reference bitmap to be traced.

    giko_glyph_map_t *map:      Glyph map used to trace the reference.

    float chunk_greed:          Float from 0 to 1. When working with
                                proportional fonts, increase this value to
                                discourage the use of "chunky" (wide)
                                characters.
                                Set to 0 when working with monospace fonts.
                                Set to 0 when working with proportional fonts to
                                only use characters with maximum nominal width.
                                Improves performance time.

    float glyph_greed:          Float from 0 to 1. Increase this value to spend
                                more time finding the best character for each
                                chunk.
                                Set to 1 to test every character in the charset.
                                Hampers performance time.

    float noise_threshold:      Float from 0 to 1. Increase this value to ignore
                                trivial and isolated features in the reference
                                image.
                                Increasing this value too much may cause a loss
                                of features.
                                Optimal value for references without noise is
                                ~0.05.
                                Improves performance time.

    int
    (*fidelity_function)(int):  Function for fitting glyph to reference image.
                                The faster the scaling, the higher fidelity the
                                tracing will be.
                                Set to NULL for the default function
                                (default is quadratic i.e. f(x) = x * x).

Output:
    - Returns an array of giko_codepoint_t terminated with 0.
    - Returns NULL if an error is encountered. Errors printed to stderr.
 */
giko_codepoint_t *giko_new_art_str(giko_bitmap_t *reference,
                                   giko_glyph_map_t *map, float chunk_greed,
                                   float glyph_greed, float noise_threshold,
                                   int (*fidelity_function)(int));

/*
    Converts a giko_codepoint_t to a utf8 byte sequence.
Input:
    uint8_t *destination:       Pointer to the destination array of the utf8
                                sequence.

    giko_codepoint_t codepoint: Codepoint to be converted.

Output:
    - Returns the number of bytes used to represent the utf8 sequence.
    - Sets the first bytes of the destination array to utf8 bytes.
    - Returns 0 if an error is encountered. Errors printed to stderr.
 */
int giko_codepoint_to_utf8(uint8_t *destination, giko_codepoint_t codepoint);

/*
    Free a giko bitmap.
Input:
    giko_bitmap_t *bitmap:  Bitmap to be freed.

Output:
    - No output.
 */
void giko_free_bitmap(giko_bitmap_t *bitmap);

/*
    Free a giko glyph map.
Input:
    giko_glyph_map_t *map:  Glyph map to be freed.

Output:
    - No output.
 */
void giko_free_glyph_map(giko_glyph_map_t *map);

// Bitmap utility

/*
    Vertically flip a giko bitmap.
Input:
    giko_bitmap_t *bitmap:  Bitmap to be flipped.

Output:
    - Bitmap pixel data is vertically flipped in-place.
 */
void giko_flip_bitmap(giko_bitmap_t *bitmap);

/*
    Negate the pixels of a giko bitmap.
Input:
    giko_bitmap_t *bitmap:  Bitmap to be negated.

Output:
    - Set pixels become unset and unset pixels become set. Done in-place.
 */
void giko_negate_bitmap(giko_bitmap_t *bitmap);

/*
    Get a patch of a bitmap.
Input:
    giko_bitmap_t *bitmap:  Bitmap to be cropped.
    int offset_x:           X offset of the top-left corner of the bounding box
                            (0 is the left-most column).
    int offset_y:           Y offset of the top-left corner of the bounding box
                            (0 is the top-most row).
    int width:              Width of the bounding box (projecting rightwards).
    int width:              Height of the bounding box (projecting downward).

Output:
    - Set pixels become unset and unset pixels become set. Done in-place.
 */
giko_bitmap_t *giko_crop_bitmap(giko_bitmap_t *bitmap, int offset_x,
                                int offset_y, int width, int height);

// File utility

/*
    Read codepoints line-by-line from a file.

Input:
    char *filepath:     String representing filepath to character set.
                        Character set file should have each codepoint number
                        on a seperate line. The prefix "U+" should be ommitted.
    int base_encoding:  The base representation of the unicode i.e. 10 for
                        decimal codepoints, 16 for hexadecimal codepoints.
                        See strtol for more detail on how different base numbers
                        are represented in string.

Output:
    - Returns an array of giko_codepoint_t terminated with 0.
    - Returns NULL if an error is encountered. Errors printed to stderr.
 */
giko_codepoint_t *giko_load_charset(char *filepath, int base_encoding);

/*
    Read codepoints line-by-line from a file.

Input:
    char *filepath:     String representing filepath to character set.
                        Character set file should have each codepoint number
                        on a seperate line. The prefix "U+" should be ommitted.
    int base_encoding:  The base representation of the unicode i.e. 10 for
                        decimal codepoints, 16 for hexadecimal codepoints.
                        See strtol for more detail on how different base numbers
                        are represented in string.

Output:
    - Returns a pointer to a giko_bitmap_t.
    - Returns NULL if an error is encountered. Errors printed to stderr.
 */
giko_bitmap_t *giko_load_bitmap(char *bmp_filepath);

/*
    Write codepoints to a file in utf8 encoding.

Input:
    giko_codepoint_t *string:   Array of codepoints terminated by 0.

    char *out_filepath:         String representing filepath to the output file.

Output:
    - Returns EXIT_SUCCESS (1) if write is successful.
    - Returns EXIT_FAILURE (0) if write is unsuccessful.
 */
int giko_write_codepoint_str(giko_codepoint_t *string, char *out_filepath);

#ifdef _cplusplus
}
#endif

#endif

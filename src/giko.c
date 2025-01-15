#include "giko.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define LINE_FEED 10
#define MAX_DIGITS_IN_CODEPOINT 8
#define STRING_CHUNK_SIZE 256
#define TERMINAL_CODEPOINT 0

// Node of a linked list
typedef struct giko_glyph {
    giko_codepoint_t codepoint;
    int advance;
    giko_bitmap_t *bitmap;
    struct giko_glyph *next;
} giko_glyph_t;

// Wrapper for an array of giko_glyph linked lists, stored in `glyphs`.
// The array is indexed by the glyphs' width (aka advance).
// E.g. The head of the linked list with glyphs of 16 pixel advance is stored
// in glyphs[16].
struct giko_glyph_map {
    int num_advances;
    int em_height;
    giko_glyph_t **glyphs;
};

typedef struct giko_match {
    int codepoint;
    int advance;
    float similarity;
} giko_match_t;

// Precomputation for performance
const int set_bits[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4,
    2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4,
    2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6,
    4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5,
    3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6,
    4, 5, 5, 6, 5, 6, 6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};

// Prototypes

giko_glyph_t *new_glyph(FT_Face face, giko_codepoint_t codepoint);

giko_bitmap_t *new_glyph_bitmap(FT_Face face, giko_codepoint_t codepoint);

giko_glyph_t *insert_glyph(giko_glyph_t *glyph, giko_glyph_t *head,
                           sort_order_t order);

giko_match_t best_scanline_match(giko_bitmap_t *reference,
                                 giko_glyph_map_t *map, int x, int y,
                                 float chunk_greed, float glyph_greed,
                                 float noise_threshold,
                                 int (*fidelity_function)(int));

giko_match_t patch_match(giko_bitmap_t *reference, giko_glyph_t *head,
                         float glyph_greed, float noise_threshold,
                         int (*fidelity_function)(int));

float bitmap_similarity(giko_bitmap_t *reference, giko_bitmap_t *bitmap,
                        float noise_threshold, int (*fidelity_function)(int));

void free_glyph_list(giko_glyph_t *list);


// Helper functions

int num_set_pixels(uint8_t pixel_byte) { return set_bits[pixel_byte]; }

int smaller_than(int x, int y) { return x < y; };

int larger_than(int x, int y) { return x > y; };

int return_true(int x, int y) { (void)x; (void)y; return 1; }

int quadratic(int x) { return x * x; }

int pitch_32bit(int width) { return ((width + 31) / 32) * 4; }

int floor_frac_pixel(long frac_pixel) { return frac_pixel >> 6; }

// Main functions

giko_bitmap_t *giko_new_bitmap(int width, int height, uint8_t *data) {
    giko_bitmap_t *bitmap = malloc(sizeof(giko_bitmap_t));
    if (!bitmap) {
        perror("Error allocating memory");
        return NULL;
    }

    int pitch = pitch_32bit(width);
    bitmap->width = width;
    bitmap->pitch = pitch;
    bitmap->height = height;
    bitmap->buffer_size = height * pitch;
    bitmap->real_size = height * width;
    bitmap->data = data;

    int num_pixels = 0;
    for (int i = 0; i < bitmap->buffer_size; i++) {
        num_pixels += num_set_pixels(bitmap->data[i]);
    }
    bitmap->set_pixels = num_pixels;

    return bitmap;
}

void giko_flip_bitmap(giko_bitmap_t *bitmap) {
    int pitch = bitmap->pitch;
    int height = bitmap->height;
    for (int row = 0; row < height / 2; row++) {
        int mirror = height - row - 1;
        int row1 = row * pitch;
        int row2 = mirror * pitch;
        for (int byte = 0; byte < pitch; byte++) {
            uint8_t byte1 = bitmap->data[row1 + byte];
            uint8_t byte2 = bitmap->data[row2 + byte];
            bitmap->data[row1 + byte] = byte2;
            bitmap->data[row2 + byte] = byte1;
        }
    }
}

void giko_negate_bitmap(giko_bitmap_t *bitmap) {
    int pitch = bitmap->pitch;
    for (int row = 0; row < bitmap->height; row++) {
        for (int byte_index = 0; byte_index < pitch; byte_index++) {
            bitmap->data[row * pitch + byte_index] ^= 0xFF; // Negate byte
        }
    }
}

giko_bitmap_t *giko_crop_bitmap(giko_bitmap_t *bitmap, int x_offset,
                                int y_offset, int width, int height) {
    assert(x_offset >= 0);
    assert(y_offset >= 0);
    assert(width >= 0);
    assert(height >= 0);

    int pitch = pitch_32bit(width);
    uint8_t *pixel_data = calloc(pitch * height, sizeof(int8_t));
    if (!pixel_data) {
        perror("Error allocating memory");
        return NULL;
    }

    // Copy patch
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src_x = x + x_offset;
            int src_y = y + y_offset;

            if (src_x < bitmap->width && src_y < bitmap->height) {
                int src_byte_index = src_y * bitmap->pitch + (src_x / 8);
                int src_bit_index = src_x % 8;
                int dst_byte_index = y * pitch + (x / 8);
                int dst_bit_index = x % 8;

                int8_t src_bit_mask = (1 << (7 - src_bit_index));
                int8_t dst_bit_mask = (1 << (7 - dst_bit_index));

                if (bitmap->data[src_byte_index] & src_bit_mask) {
                    pixel_data[dst_byte_index] |= dst_bit_mask;
                }
            }
        }
    }

    return giko_new_bitmap(width, height, pixel_data);
}

giko_glyph_map_t *giko_new_glyph_map(char *ttf_filepath,
                                     giko_codepoint_t *charset, int glyph_size,
                                     sort_order_t order) {
    assert(glyph_size > 0);
    assert(0 <= order && 3 >= order);

    // Check if filepath exists
    FILE *f = fopen(ttf_filepath, "r");
    if (!f) {
        perror(ttf_filepath);
        return NULL;
    }
    fclose(f);

    giko_glyph_map_t *map = malloc(sizeof(giko_glyph_map_t));
    if (!map) {
        perror("Error allocating memory");
        return NULL;
    }

    FT_Library library;
    FT_Face face;
    FT_Init_FreeType(&library);
    FT_New_Face(library, ttf_filepath, 0, &face);
    FT_Set_Pixel_Sizes(face, 0, glyph_size);

    int max_advance = floor_frac_pixel(face->size->metrics.max_advance) + 1;
    map->num_advances = max_advance;
    map->em_height = floor_frac_pixel(face->size->metrics.height);

    map->glyphs = calloc(max_advance, sizeof(giko_glyph_t *));
    if (!map->glyphs) {
        perror("Error allocating memory");
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return NULL;
    }

    int index = 0;
    int codepoint = charset[index];
    while (codepoint != TERMINAL_CODEPOINT) {
        giko_glyph_t *glyph = new_glyph(face, codepoint);
        if (!glyph) {
            index++;
            codepoint = charset[index];
            continue;
        }
        int advance = glyph->advance;

        map->glyphs[advance] =
            insert_glyph(glyph, map->glyphs[advance], DESCENDING);

        index++;
        codepoint = charset[index];
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return map;
}

giko_glyph_t *new_glyph(FT_Face face, giko_codepoint_t codepoint) {
    giko_glyph_t *glyph = malloc(sizeof(giko_glyph_t));
    if (!glyph) {
        perror("Error allocating memory");
        return NULL;
    }
    glyph->codepoint = codepoint;
    glyph->bitmap = new_glyph_bitmap(face, codepoint);
    if (!glyph->bitmap) {
        free(glyph);
        return NULL;
    }
    glyph->advance = glyph->bitmap->width;
    glyph->next = NULL;

    return glyph;
}


giko_glyph_t *insert_glyph(giko_glyph_t *glyph, giko_glyph_t *head,
                           sort_order_t order) {
    if (head == NULL) {
        return glyph;
    }

    int (*conditional)(int, int) = return_true;
    if (order == ASCENDING)
        conditional = smaller_than;
    if (order == DESCENDING)
        conditional = larger_than;

    if (conditional(glyph->bitmap->set_pixels, head->bitmap->set_pixels)) {
        // Insert at head
        giko_glyph_t *tmp = head;
        glyph->next = tmp;
        return glyph;
    }

    giko_glyph_t *prev = head;
    giko_glyph_t *curr = head->next;

    if (curr == NULL) {
        // Append glyph on one-node list
        head->next = glyph;
        return head;
    }

    while (curr->next != NULL) {
        if (conditional(glyph->bitmap->set_pixels, curr->bitmap->set_pixels))
            break;
        prev = curr;
        curr = curr->next;
    }

    // Insert or append glyph
    glyph->next = curr;
    prev->next = glyph;
    return head;
}

giko_bitmap_t *new_glyph_bitmap(FT_Face face, giko_codepoint_t codepoint) {
    FT_Long glyph_index = FT_Get_Char_Index(face, codepoint);
    if (!glyph_index) {
        return NULL;
    }
    FT_Load_Glyph(face, glyph_index, FT_LOAD_MONOCHROME);

    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
    FT_Bitmap *src_bitmap = &face->glyph->bitmap;

    int width = floor_frac_pixel(face->glyph->metrics.horiAdvance);
    int height = floor_frac_pixel(face->size->metrics.height);
    int pitch = pitch_32bit(width);

    uint8_t *pixel_data = calloc(height * pitch, sizeof(uint8_t));
    if (!pixel_data) {
        perror("Error allocating memory");
        return NULL;
    }

    int ascent = floor_frac_pixel(face->size->metrics.ascender);
    int x_offset = face->glyph->bitmap_left;
    int y_offset = ascent - face->glyph->bitmap_top;

    for (unsigned int y = 0; y < src_bitmap->rows; y++) {
        for (unsigned int x = 0; x < src_bitmap->width; x++) {
            int dst_x = x + x_offset;
            int dst_y = y + y_offset;

            if (dst_x < width && dst_y < height) {
                int src_byte_index = y * src_bitmap->pitch + (x / 8);
                int src_bit_index = x % 8;
                int dst_byte_index = dst_y * pitch + (dst_x / 8);
                int dst_bit_index = dst_x % 8;

                int8_t src_bit_mask = (1 << (7 - src_bit_index));
                int8_t dst_bit_mask = (1 << (7 - dst_bit_index));

                if (src_bitmap->buffer[src_byte_index] & src_bit_mask) {
                    pixel_data[dst_byte_index] |= dst_bit_mask;
                }
            }
        }
    }

    return giko_new_bitmap(width, height, pixel_data);
}

giko_codepoint_t *giko_new_art_str(giko_bitmap_t *reference,
                                   giko_glyph_map_t *map, float chunk_greed,
                                   float glyph_greed, float noise_threshold,
                                   int (*fidelity_function)(int)) {
    assert(0 < chunk_greed && 1 >= chunk_greed);
    assert(0 < glyph_greed && 1 >= glyph_greed);
    assert(0 <= noise_threshold && 1 >= noise_threshold);

    if (fidelity_function == NULL)
        fidelity_function = quadratic;

    int size = 0;
    int capacity = STRING_CHUNK_SIZE;
    giko_codepoint_t *codepoints = malloc(capacity * sizeof(giko_codepoint_t));
    if (!codepoints) {
        perror("Error allocating memory");
        return NULL;
    }

    int height = reference->height;
    int width = reference->width;
    int em_height = map->em_height;
    int rows = (height + (em_height - 1)) / em_height; // Ceiling function

    for (int row = 0; row < rows; row++) {
        int x = 0;
        while (x < width) {
            if (size >= capacity - 1) {
                capacity += STRING_CHUNK_SIZE;
                codepoints =
                    realloc(codepoints, capacity * sizeof(giko_codepoint_t));
                if (!codepoints) {
                    perror("Error allocating memory");
                    return NULL;
                }
            }
            int y = row * em_height;
            giko_match_t best_match = best_scanline_match(
                reference, map, x, y, chunk_greed, glyph_greed, noise_threshold,
                fidelity_function);
            codepoints[size] = best_match.codepoint;
            size++;
            x += best_match.advance;
        }
        codepoints[size] = LINE_FEED;
        size++;
    }

    codepoints[size] = 0;
    return codepoints;
}

giko_match_t best_scanline_match(giko_bitmap_t *reference,
                                 giko_glyph_map_t *map, int x, int y,
                                 float chunk_greed, float glyph_greed,
                                 float noise_threshold,
                                 int (*fidelity_function)(int)) {
    assert(x >= 0);
    assert(y >= 0);

    giko_match_t best_match = {0};
    int advance = map->num_advances - 1;
    giko_bitmap_t *patch;
    while (advance > 0 && best_match.similarity < chunk_greed) {
        patch = giko_crop_bitmap(reference, x, y, advance, map->em_height);

        giko_glyph_t *list = map->glyphs[advance];
        if (!list) {
            advance--;
            continue;
        }

        giko_match_t match = patch_match(patch, list, glyph_greed,
                                         noise_threshold, fidelity_function);

        if (match.similarity >= best_match.similarity) {
            best_match = match;
        }
        advance--;
    }

    giko_free_bitmap(patch);
    return best_match;
}

giko_match_t patch_match(giko_bitmap_t *reference, giko_glyph_t *head,
                         float glyph_greed, float noise_threshold,
                         int (*fidelity_function)(int)) {
    giko_match_t best_match = {0};
    best_match.advance = head->advance;
    giko_glyph_t *curr;
    for (curr = head; curr != NULL; curr = curr->next) {
        float similarity = bitmap_similarity(
            reference, curr->bitmap, noise_threshold, fidelity_function);
        if (similarity >= best_match.similarity) {
            best_match.similarity = similarity;
            best_match.codepoint = curr->codepoint;

            if (similarity >= glyph_greed) {
                return best_match;
            }
        }
    }

    return best_match;
}

float bitmap_similarity(giko_bitmap_t *reference, giko_bitmap_t *bitmap,
                        float noise_threshold, int (*fidelity_function)(int)) {
    assert(reference->height == bitmap->height);
    assert(reference->pitch == bitmap->pitch);

    int reference_set_pixels = reference->set_pixels;
    int bitmap_set_pixels = bitmap->set_pixels;
    int overlapping_pixels = 0;

    for (int i = 0; i < bitmap->buffer_size; i++) {
        uint8_t reference_byte = reference->data[i];
        uint8_t bitmap_byte = bitmap->data[i];
        overlapping_pixels += num_set_pixels(reference_byte & bitmap_byte);
    }

    int empty_glyph = bitmap_set_pixels == 0;
    int max_noise_pixels = noise_threshold * bitmap->real_size;
    if (empty_glyph && reference_set_pixels <= max_noise_pixels) {
        return 1;
    }

    int extranuous_pixels = bitmap_set_pixels - overlapping_pixels;
    int extranuous_penalty = fidelity_function(extranuous_pixels);
    int set_pixels =
        reference_set_pixels + bitmap_set_pixels - overlapping_pixels;

    return (float)overlapping_pixels / (set_pixels + extranuous_penalty);
}

void giko_free_bitmap(giko_bitmap_t *bitmap) {
    free(bitmap->data);
    free(bitmap);
}

void giko_free_glyph_map(giko_glyph_map_t *map) {
    for (int i = 0; i < map->num_advances; i++) {
        free_glyph_list(map->glyphs[i]);
    }
    free(map->glyphs);
    free(map);
}

void free_glyph_list(giko_glyph_t *list) {
    giko_glyph_t *curr;
    giko_glyph_t *tmp = NULL;
    for (curr = list; curr != NULL; curr = tmp) {
        tmp = curr->next;
        giko_free_bitmap(curr->bitmap);
        free(curr);
    }
}

int giko_codepoint_to_utf8(uint8_t *destination, giko_codepoint_t codepoint) {
    if (codepoint <= 0x7F) {
        destination[0] = codepoint;
        return 1;
    }
    if (codepoint <= 0x7FF) {
        destination[0] = 0xC0 | (codepoint >> 6);
        destination[1] = 0x80 | (codepoint & 0x3F);
        return 2;
    }
    if (codepoint <= 0xFFFF) {
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF)
            return 0;
        destination[0] = 0xE0 | (codepoint >> 12);
        destination[1] = 0x80 | ((codepoint >> 6) & 0x3F);
        destination[2] = 0x80 | (codepoint & 0x3F);
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        destination[0] = 0xF0 | (codepoint >> 18);
        destination[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        destination[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        destination[3] = 0x80 | (codepoint & 0x3F);
        return 4;
    }
    return 0;
}

giko_codepoint_t *giko_load_charset(char *filepath, int base_encoding) {
    assert(base_encoding > 0);

    int capacity = STRING_CHUNK_SIZE;
    int size = 0;
    giko_codepoint_t *codepoints = malloc(capacity * sizeof(giko_codepoint_t));
    if (!codepoints) {
        perror("Error allocating memory");
        return NULL;
    }

    FILE *charset_f = fopen(filepath, "rb");
    if (!charset_f) {
        perror(filepath);
        return NULL;
    }

    errno = 0;
    char *endptr;
    char codepoint_str[MAX_DIGITS_IN_CODEPOINT];

    while (fgets(codepoint_str, MAX_DIGITS_IN_CODEPOINT, charset_f)) {
        int codepoint = strtol(codepoint_str, &endptr, base_encoding);

        if (errno) {
            perror(codepoint_str);
            return NULL;
        }
        if (*endptr == codepoint)
            continue;

        if (size >= capacity - 1) {
            capacity += STRING_CHUNK_SIZE;
            codepoints =
                realloc(codepoints, capacity * sizeof(giko_codepoint_t));
            if (!codepoints) {
                perror("Error allocating memory");
                fclose(charset_f);
                return NULL;
            }
        }
        codepoints[size] = codepoint;
        size++;
    }

    fclose(charset_f);
    codepoints[size] = TERMINAL_CODEPOINT;
    return codepoints;
}

giko_bitmap_t *giko_load_bitmap(char *bmp_filepath) {
    FILE *bitmap_f = fopen(bmp_filepath, "rb");
    if (!bitmap_f) {
        perror(bmp_filepath);
        return NULL;
    }

    int width;
    int height;

    fseek(bitmap_f, 18, SEEK_SET);
    fread(&width, 4, 1, bitmap_f);
    fread(&height, 4, 1, bitmap_f);

    uint32_t pixel_data_offset;
    fseek(bitmap_f, 10, SEEK_SET);
    fread(&pixel_data_offset, 4, 1, bitmap_f);

    int bitmap_size = pitch_32bit(width) * height;
    uint8_t *pixel_data = malloc(bitmap_size * sizeof(uint8_t));
    if (!pixel_data) {
        perror("Error allocating memory");
        fclose(bitmap_f);
        return NULL;
    }
    fseek(bitmap_f, pixel_data_offset, SEEK_SET);
    fread(pixel_data, 1, bitmap_size, bitmap_f);

    fclose(bitmap_f);

    giko_bitmap_t *bitmap = giko_new_bitmap(width, height, pixel_data);
    if (!bitmap)
        return NULL;
    giko_flip_bitmap(bitmap);

    return bitmap;
}

int giko_write_codepoint_str(giko_codepoint_t *string, char *out_filepath) {
    FILE *out_f = fopen(out_filepath, "w");
    if (!out_f) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    uint8_t utf8[4];
    int index = 0;
    giko_codepoint_t codepoint = string[0];

    while (codepoint != 0) {
        int length = giko_codepoint_to_utf8(utf8, codepoint);

        if (length > 0) {
            fwrite(utf8, 1, length, out_f);
        } else {
            fprintf(stderr, "Invalid codepoint: U+%04X\n", codepoint);
            fclose(out_f);
            return EXIT_FAILURE;
        }
        index++;
        codepoint = string[index];
    }

    fclose(out_f);
    return EXIT_SUCCESS;
}

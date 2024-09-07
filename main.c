#include <ft2build.h>
#include FT_FREETYPE_H

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_ART_STR_LEN 65536
#define LINE_FEED 10

typedef struct giko_bitmap {
    int width;
    int pitch;
    int height;
    uint8_t *data;
} giko_bitmap_t;

typedef struct giko_glyph {
    long codepoint;
    int advance;
    giko_bitmap_t *bitmap;
    struct giko_glyph *next;
} giko_glyph_t;

typedef struct giko_advance_map {
    int size;
    int em_height;
    giko_glyph_t **advances;
} giko_glyph_map_t;

typedef struct giko_match {
    long codepoint;
    int advance;
    double similarity;
} giko_match_t;

typedef struct giko_unicode_str {
    int size;
    long *codepoints;
} giko_unicode_str_t;

// Function prototypes
giko_bitmap_t *new_image_bitmap(char *filepath);
giko_glyph_map_t *new_glyph_map(FT_Face face, char *filename);
giko_glyph_t *new_glyph(FT_Face face, long codepoint);
giko_bitmap_t *new_glyph_bitmap(FT_Face face, long codepoint);
int floor_frac_pixel(long fractional_pixel_count);
int pitch_32bit(int width);
int num_set_pixels(int8_t pixel_byte);
giko_unicode_str_t *new_art_str(giko_bitmap_t *reference, giko_glyph_map_t *map,
                                double greed);
giko_match_t best_scanline_match(giko_bitmap_t *reference,
                                 giko_glyph_map_t *map, int x, int y,
                                 double greed);
giko_bitmap_t *crop_bitmap(giko_bitmap_t *bitmap, int offset_x, int offset_y,
                           int width, int height);
giko_match_t patch_match(giko_bitmap_t *reference, giko_glyph_t *head);
double bitmap_similarity(giko_bitmap_t *bitmap1, giko_bitmap_t *bitmap2);

void free_glyph_map(giko_glyph_map_t *map);
void free_glyph_list(giko_glyph_t *list);
void free_bitmap(giko_bitmap_t *bitmap);
void free_unicode_str(giko_unicode_str_t *unicode_str);

void print_bitmap(giko_bitmap_t *bitmap);
void print_unicode_str(giko_unicode_str_t *unicode_str);
unsigned convert_unicode_to_utf8(uint8_t *utf8, uint32_t codepoint);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: ascii_tracer <fontface.ttf> <charset.txt> "
                        "<reference.bmp>\n");
        return 1;
    }
    giko_bitmap_t *reference = new_image_bitmap(argv[3]);

    FT_Library library;
    FT_Face face;
    FT_Init_FreeType(&library);
    FT_New_Face(library, argv[1], 0, &face);
    FT_Set_Pixel_Sizes(face, 16, 16);

    giko_glyph_map_t *glyph_map = new_glyph_map(face, argv[2]);
    giko_unicode_str_t *aa = new_art_str(reference, glyph_map, 0.3);
    print_unicode_str(aa);

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    free_bitmap(reference);
    free_glyph_map(glyph_map);
    free_unicode_str(aa);
    return 0;
}

giko_bitmap_t *new_image_bitmap(char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        perror("Error opening image file");
        return NULL;
    }

    giko_bitmap_t *image = malloc(sizeof(giko_bitmap_t));

    fseek(file, 18, SEEK_SET);
    fread(&image->width, 4, 1, file);
    fread(&image->height, 4, 1, file);

    uint32_t image_data_offset;
    fseek(file, 10, SEEK_SET);
    fread(&image_data_offset, 4, 1, file);

    int pitch = pitch_32bit(image->width);
    int image_size = pitch * image->height;
    image->pitch = pitch;

    uint8_t *pixel_array = malloc(image_size);
    fseek(file, image_data_offset, SEEK_SET);
    fread(pixel_array, 1, image_size, file);

    // Flip image vertically
    for (int row = 0; row < image->height / 2; row++) {
        int mirror = image->height - row - 1;
        int row1 = row * pitch;
        int row2 = mirror * pitch;
        for (int byte = 0; byte < pitch; byte++) {
            uint8_t byte1 = pixel_array[row1 + byte];
            uint8_t byte2 = pixel_array[row2 + byte];
            pixel_array[row1 + byte] = byte2;
            pixel_array[row2 + byte] = byte1;
        }
    }

    image->data = pixel_array;

    fclose(file);
    return image;
}

giko_glyph_map_t *new_glyph_map(FT_Face face, char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        perror("Error opening charset file");
        return NULL;
    }

    giko_glyph_map_t *map = malloc(sizeof(giko_glyph_map_t));
    int max_advance = floor_frac_pixel(face->size->metrics.max_advance);
    map->size = max_advance;
    map->em_height = floor_frac_pixel(face->size->metrics.height);
    map->advances = calloc(max_advance + 1, sizeof(giko_glyph_t *));

    char codepoint_str[16];
    while (fscanf(file, "%s ", codepoint_str) == 1) {
        // PERFORMANCE: Density indexing
        long codepoint = strtol(codepoint_str, NULL, 10);
        giko_glyph_t *glyph = new_glyph(face, codepoint);
        int advance = glyph->advance;
        giko_glyph_t *head = map->advances[advance];
        glyph->next = head;
        map->advances[advance] = glyph;
    }

    fclose(file);
    return map;
}

giko_glyph_t *new_glyph(FT_Face face, long codepoint) {
    giko_glyph_t *glyph = malloc(sizeof(giko_glyph_t));
    glyph->codepoint = codepoint;
    glyph->bitmap = new_glyph_bitmap(face, codepoint);
    glyph->advance = glyph->bitmap->width;
    glyph->next = NULL;

    return glyph;
}

giko_bitmap_t *new_glyph_bitmap(FT_Face face, long codepoint) {
    FT_Long glyph_index = FT_Get_Char_Index(face, codepoint);
    FT_Load_Glyph(face, glyph_index, FT_LOAD_MONOCHROME);

    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
    FT_Bitmap *src_bitmap = &face->glyph->bitmap;

    int width = floor_frac_pixel(face->glyph->metrics.horiAdvance);
    int height = face->size->metrics.y_ppem;
    int pitch = pitch_32bit(width);

    giko_bitmap_t *bitmap = malloc(sizeof(giko_bitmap_t));
    bitmap->width = width;
    bitmap->pitch = pitch;
    bitmap->height = height;
    bitmap->data = calloc(bitmap->height * pitch, sizeof(uint8_t));

    int ascent = floor_frac_pixel(face->size->metrics.ascender);
    int x_offset = face->glyph->bitmap_left;
    int y_offset = ascent - face->glyph->bitmap_top;

    for (int y = 0; y < src_bitmap->rows; y++) {
        for (int x = 0; x < src_bitmap->width; x++) {
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
                    bitmap->data[dst_byte_index] |= dst_bit_mask;
                }
            }
        }
    }

    return bitmap;
}

int floor_frac_pixel(long fractional_pixel_count) {
    return fractional_pixel_count >> 6;
}

int pitch_32bit(int width) { return ((width + 31) / 32) * 4; }

int num_set_pixels(int8_t pixel_byte) {
    // PERFORMANCE: Hashmap
    int count = 0;
    for (int i = 0; i < 8; i++) {
        if (pixel_byte & (1 << i)) {
            count++;
        }
    }
    return count;
}

giko_unicode_str_t *new_art_str(giko_bitmap_t *reference, giko_glyph_map_t *map,
                                double greed) {
    long *codepoints = calloc(MAX_ART_STR_LEN, sizeof(long));
    int height = reference->height;
    int width = reference->width;
    int em_height = map->em_height;
    int rows = (height + (em_height - 1)) / em_height; // Ceiling function

    int num_patches = 0;
    for (int row = 0; row < rows; row++) {
        int x = 0;
        while (x < width) {
            int y = row * em_height;
            giko_match_t best_match =
                best_scanline_match(reference, map, x, y, greed);
            codepoints[num_patches] = best_match.codepoint;
            x += best_match.advance + 1;
            num_patches++;
        }
        codepoints[num_patches] = LINE_FEED;
        num_patches++;
    }

    giko_unicode_str_t *unicode_str = malloc(sizeof(giko_unicode_str_t));
    unicode_str->size = num_patches;
    unicode_str->codepoints = codepoints;
    return unicode_str;
}

giko_match_t best_scanline_match(giko_bitmap_t *reference,
                                 giko_glyph_map_t *map, int x, int y,
                                 double greed) {
    giko_match_t best_match = {0};
    int advance = map->size;
    while (advance > 0 && best_match.similarity < greed) {
        giko_glyph_t *list = map->advances[advance];
        if (!list) {
            advance--;
            continue;
        }
        giko_bitmap_t *patch =
            crop_bitmap(reference, x, y, advance, map->em_height);
        giko_match_t match = patch_match(patch, list);
        if (match.similarity >= best_match.similarity) {
            best_match.advance = advance;
            best_match.codepoint = match.codepoint;
            best_match.similarity = match.similarity;
        }
        advance--;
    }
    return best_match;
}

giko_bitmap_t *crop_bitmap(giko_bitmap_t *bitmap, int x_offset, int y_offset,
                           int width, int height) {
    assert(x_offset >= 0);
    assert(y_offset >= 0);
    assert(width >= 0);
    assert(height >= 0);

    giko_bitmap_t *patch = malloc(sizeof(giko_bitmap_t));
    int pitch = pitch_32bit(width);
    patch->width = width;
    patch->height = height;
    patch->pitch = pitch;
    patch->data = calloc(pitch * height, sizeof(int8_t));

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
                    patch->data[dst_byte_index] |= dst_bit_mask;
                }
            }
        }
    }
    return patch;
}

giko_match_t patch_match(giko_bitmap_t *reference, giko_glyph_t *head) {
    giko_match_t best_match = {0};
    best_match.advance = head->advance;
    giko_glyph_t *curr;
    for (curr = head; curr != NULL; curr = curr->next) {
        double similarity = bitmap_similarity(reference, curr->bitmap);
        // PERFORMANCE: Add greed
        if (similarity >= best_match.similarity) {
            best_match.similarity = similarity;
            best_match.codepoint = curr->codepoint;
        }
    }

    return best_match;
}

double bitmap_similarity(giko_bitmap_t *bitmap1, giko_bitmap_t *bitmap2) {
    assert(bitmap1->height == bitmap2->height);
    assert(bitmap1->pitch == bitmap2->pitch);

    double total_set_pixels = 0;
    double overlapping_set_pixels = 0;

    for (int i = 0; i < bitmap1->pitch * bitmap1->height; i++) {
        int8_t byte_1 = bitmap1->data[i];
        int8_t byte_2 = bitmap2->data[i];
        overlapping_set_pixels += num_set_pixels(byte_1 & byte_2);
        total_set_pixels += num_set_pixels(byte_1 | byte_2);
    }

    if (total_set_pixels == 0) {
        return 1;
    }

    return overlapping_set_pixels / total_set_pixels;
}

void free_glyph_map(giko_glyph_map_t *map) {
    for (int i = 0; i < map->size; i++) {
        free_glyph_list(map->advances[i]);
    }
    free(map->advances);
    free(map);
}

void free_glyph_list(giko_glyph_t *list) {
    giko_glyph_t *curr;
    giko_glyph_t *tmp = NULL;
    for (curr = list; curr != NULL; curr = tmp) {
        tmp = curr->next;
        free_bitmap(curr->bitmap);
        free(curr);
    }
}

void free_bitmap(giko_bitmap_t *bitmap) {
    free(bitmap->data);
    free(bitmap);
}

void free_unicode_str(giko_unicode_str_t *unicode_str) {
    free(unicode_str->codepoints);
    free(unicode_str);
}

void print_bitmap(giko_bitmap_t *bitmap) {
    for (int y = 0; y < bitmap->height; y++) {
        for (int x = 0; x < bitmap->width; x++) {
            int byte = bitmap->data[y * bitmap->pitch + (x / 8)];
            char pixel = (byte & (1 << (7 - (x % 8)))) ? '#' : '.';
            printf("%c", pixel);
        }
        printf("\n");
    }
}

void print_unicode_str(giko_unicode_str_t *unicode_str) {
    uint8_t utf8[4];

    for (int i = 0; i < unicode_str->size; i++) {
        uint32_t codepoint = (uint32_t)unicode_str->codepoints[i];
        unsigned length = convert_unicode_to_utf8(utf8, codepoint);

        if (length > 0) {
            fwrite(utf8, 1, length, stdout);
        } else {
            fprintf(stderr, "Invalid codepoint: U+%04X\n", codepoint);
        }
    }

    printf("\n");
}

// https://stackoverflow.com/questions/38491380/how-to-print-unicode-codepoints-as-characters-in-c
unsigned convert_unicode_to_utf8(uint8_t *utf8, uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        utf8[0] = codepoint;
        return 1;
    }
    if (codepoint <= 0x7FF) {
        utf8[0] = 0xC0 | (codepoint >> 6);
        utf8[1] = 0x80 | (codepoint & 0x3F);
        return 2;
    }
    if (codepoint <= 0xFFFF) {
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF)
            return 0;
        utf8[0] = 0xE0 | (codepoint >> 12);
        utf8[1] = 0x80 | ((codepoint >> 6) & 0x3F);
        utf8[2] = 0x80 | (codepoint & 0x3F);
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        utf8[0] = 0xF0 | (codepoint >> 18);
        utf8[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        utf8[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        utf8[3] = 0x80 | (codepoint & 0x3F);
        return 4;
    }
    return 0;
}

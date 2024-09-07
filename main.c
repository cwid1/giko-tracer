#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdio.h>
#include <stdint.h>
#include <assert.h>

typedef struct giko_bitmap {
    int width;
    int pitch;
    int rows;
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
    giko_glyph_t **advances;
} giko_glyph_map_t;

typedef struct giko_score {
    long codepoint;
    int advance;
    double similarity;
} giko_score_t;

// Function prototypes
giko_bitmap_t *new_image_bitmap(char *filepath);
giko_bitmap_t *new_glyph_bitmap(FT_Face face, long codepoint);
giko_bitmap_t *crop_bitmap(giko_bitmap_t *bitmap, int x, int y, int width, int height);
giko_glyph_map_t *new_glyph_map(FT_Face face, char *filename);
giko_glyph_t *new_glyph(FT_Face face, long codepoint);
giko_score_t best_match(giko_bitmap_t *reference, giko_glyph_t *head);
double bitmap_similarity(giko_bitmap_t *bitmap1, giko_bitmap_t *bitmap2);
int num_set_pixels(int8_t pixel_byte);
int floor_frac_pixel(long fractional_pixel_count);
int pitch_32bit(int width);
void free_glyph_map(giko_glyph_map_t *map);
void free_glyph_list(giko_glyph_t *list);
void free_bitmap(giko_bitmap_t *bitmap);
void print_bitmap(giko_bitmap_t *bitmap);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: ascii_tracer <fontface.ttf> <charset.txt> <reference.bmp>\n");
        return 1;
    }
    giko_bitmap_t *reference = new_image_bitmap(argv[3]);

    FT_Library library;
    FT_Face face;
    FT_Init_FreeType(&library);
    FT_New_Face(library, argv[1], 0, &face);
    FT_Set_Pixel_Sizes(face, 16, 16);

    giko_bitmap_t *chunk = crop_bitmap(reference, 0, 0, 16, 16);
    
    giko_glyph_map_t *glyph_map = new_glyph_map(face, argv[2]);
    giko_score_t best = best_match(chunk, glyph_map->advances[16]);
    printf("%lu, %lf\n", best.codepoint, best.similarity);

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    free_bitmap(chunk);
    free_bitmap(reference);
    free_glyph_map(glyph_map);
    return 0;
}

void print_bitmap(giko_bitmap_t *bitmap) {
    for (int y = 0; y < bitmap->rows; y++) {
        for (int x = 0; x < bitmap->width; x++) {
            int byte = bitmap->data[y * bitmap->pitch + (x / 8)];
            char pixel = (byte & (1 << (7 - (x % 8)))) ? '#' : '.';
            printf("%c", pixel);
        }
        printf("\n");
    }
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
    fread(&image->rows, 4, 1, file);

    uint32_t image_data_offset;
    fseek(file, 10, SEEK_SET);
    fread(&image_data_offset, 4, 1, file);

    int pitch = pitch_32bit(image->width);
    int image_size = pitch * image->rows;
    image->pitch = pitch;

    uint8_t *pixel_array = malloc(image_size);
    fseek(file, image_data_offset, SEEK_SET);
    fread(pixel_array, 1, image_size, file);

    // Flip image vertically
    for (int row = 0; row < image->rows / 2; row++) {
        int mirror = image->rows - row - 1;
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

giko_bitmap_t *crop_bitmap(giko_bitmap_t *bitmap, int x_offset, int y_offset, int width, int height) {
    assert(x_offset >= 0);
    assert(y_offset >= 0);
    assert(width >= 0);
    assert(height >= 0);

    giko_bitmap_t *crop = malloc(sizeof(giko_bitmap_t));
    int pitch = pitch_32bit(width);
    crop->width = width;
    crop->rows = height;
    crop->pitch = pitch;
    crop->data = calloc(pitch * height, sizeof(int8_t));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src_x = x_offset + x;
            int src_y = y_offset + y;
            
            if (src_x < bitmap->width && src_y < bitmap->rows) {
                int src_byte_index = src_y * bitmap->pitch + (src_x / 8);
                int src_bit_index = src_x % 8;
                int dst_byte_index = y * pitch + (x / 8);
                int dst_bit_index = x % 8;

                int8_t src_bit_mask = (1 << (7 - src_bit_index));
                int8_t dst_bit_mask = (1 << (7 - dst_bit_index));

                if (bitmap->data[src_byte_index] & src_bit_mask) {
                    crop->data[dst_byte_index] |= dst_bit_mask;
                }
            }
        }
    }
    return crop;
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
    glyph->next= NULL;

    return glyph;
}

giko_bitmap_t *new_glyph_bitmap(FT_Face face, long codepoint) {
    FT_Long glyph_index = FT_Get_Char_Index(face, codepoint);

    // Load and render the glyph in monochrome mode
    FT_Load_Glyph(face, glyph_index, FT_LOAD_MONOCHROME);
    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
    FT_Bitmap *unaligned_bitmap = &face->glyph->bitmap;

    // Calculate pitch for the output bitmap
    int advance = floor_frac_pixel(face->glyph->metrics.horiAdvance);
    int pitch = pitch_32bit(advance);

    // Create and initialize the glyph_bitmap_t structure
    giko_bitmap_t *bitmap = malloc(sizeof(giko_bitmap_t));
    bitmap->width = advance;
    bitmap->pitch = pitch;
    bitmap->rows = face->size->metrics.y_ppem;
    bitmap->data = calloc(bitmap->rows * pitch, sizeof(uint8_t));

    // Calculate the offsets for positioning
    int ascent = floor_frac_pixel(face->size->metrics.ascender);
    int x_offset = face->glyph->bitmap_left;
    int y_offset = ascent - face->glyph->bitmap_top;

    // Copy and position the glyph's bitmap into the final pixel array
    for (int y = 0; y < unaligned_bitmap->rows; y++) {
        for (int x = 0; x < unaligned_bitmap->width; x++) {
            int src_byte_index = y * unaligned_bitmap->pitch + (x / 8);
            int src_bit_index = x % 8;

            int dst_x = x + x_offset;
            int dst_y = y + y_offset;

            int in_x_bounds = (dst_x >= 0 && dst_x < advance);
            int in_y_bounds = (dst_y >= 0 && dst_y < bitmap->rows);

            if (in_x_bounds && in_y_bounds) {
                int dst_byte_index = dst_y * pitch + (dst_x / 8);
                int dst_bit_index = dst_x % 8;

                int8_t src_bit_mask = (1 << (7 - src_bit_index));
                int8_t dst_bit_mask = (1 << (7 - dst_bit_index));

                if (unaligned_bitmap->buffer[src_byte_index] & src_bit_mask) {
                    bitmap->data[dst_byte_index] |= dst_bit_mask;
                }
            }
        }
    }

    return bitmap;
}

giko_score_t best_match(giko_bitmap_t *reference, giko_glyph_t *head) {
    giko_score_t best = {0};
    best.advance = head->advance;
    giko_glyph_t *curr;
    for (curr = head; curr != NULL; curr = curr->next) {
        double similarity = bitmap_similarity(reference, curr->bitmap);
        // PERFORMANCE: Add greed
        if (similarity >= best.similarity) {
            best.similarity = similarity;
            best.codepoint = curr->codepoint;
        }
    }

    return best;
}

double bitmap_similarity(giko_bitmap_t *bitmap1, giko_bitmap_t *bitmap2) {
    assert(bitmap1->rows == bitmap2->rows);
    assert(bitmap1->pitch == bitmap2->pitch);

    double total_set_pixels = 0;
    double overlapping_set_pixels = 0;

    for (int i = 0; i < bitmap1->pitch * bitmap1->rows; i++) {
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

int pitch_32bit(int width) {
    return ((width + 31) / 32) * 4;
}

int floor_frac_pixel(long fractional_pixel_count) {
    return fractional_pixel_count >> 6;
}

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


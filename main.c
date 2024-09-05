#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdio.h>
#include <stdint.h>

typedef struct {
    long codepoint;
    int advance;
    int pitch;
    int rows;
    uint8_t *pixel_array;
} at_glyph_t;

// Function prototypes
int pitch_32bit(int width);
at_glyph_t *new_glyph_from_file(char *filename);
at_glyph_t *new_glyph_from_font_face(FT_Face face, FT_Long codepoint);
int floor_frac_pixel(long fractional_pixel_count);
// double bitmap_similarity(uint8_t *image_data_1, uint8_t *image_data_2);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: ascii_tracer <fontface.ttf> <filename.bmp>\n");
        return 1;
    }
    at_glyph_t *image = new_glyph_from_file(argv[2]);

    FT_Library library;
    FT_Face face;
    FT_Init_FreeType(&library);
    FT_New_Face(library, argv[1], 0, &face);
    FT_Set_Pixel_Sizes(face, image->advance, image->rows);

    at_glyph_t *glyph = new_glyph_from_font_face(face, 13247);

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    for (int y = 0; y < image->rows; y++) {
        for (int x = 0; x < image->advance; x++) {
            int byte = image->pixel_array[y * image->pitch + (x / 8)];
            char pixel = (byte & (1 << (7 - (x % 8)))) ? '#' : '.';
            printf("%c", pixel);
        }
        printf("\n");
    }

    printf("\n");
    
    for (int y = 0; y < glyph->rows; y++) {
        for (int x = 0; x < glyph->advance; x++) {
            int byte = glyph->pixel_array[y * glyph->pitch + (x / 8)];
            char pixel = (byte & (1 << (7 - (x % 8)))) ? '#' : '.';
            printf("%c", pixel);
        }
        printf("\n");
    }

    free(image->pixel_array);
    free(image);
    free(glyph->pixel_array);
    free(glyph);
    return 0;
}

at_glyph_t *new_glyph_from_file(char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file");
        return NULL;
    }

    at_glyph_t *image = malloc(sizeof(at_glyph_t));

    fseek(file, 18, SEEK_SET);
    fread(&image->advance, 4, 1, file);
    fread(&image->rows, 4, 1, file);

    uint32_t image_data_offset;
    fseek(file, 10, SEEK_SET);
    fread(&image_data_offset, 4, 1, file);

    int pitch = pitch_32bit(image->advance);
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

    image->pixel_array = pixel_array;
    image->codepoint = 0;

    fclose(file);
    return image;
}

at_glyph_t *new_glyph_from_font_face(FT_Face face, FT_Long codepoint) {
    FT_Long glyph_index = FT_Get_Char_Index(face, codepoint);

    // Load and render the glyph in monochrome mode
    FT_Load_Glyph(face, glyph_index, FT_LOAD_MONOCHROME);
    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
    FT_Bitmap *unaligned_bitmap = &face->glyph->bitmap;

    // Calculate pitch for the output bitmap
    int advance = floor_frac_pixel(face->glyph->metrics.horiAdvance);
    int pitch = pitch_32bit(advance);

    // Create and initialize the glyph_bitmap_t structure
    at_glyph_t *glyph = malloc(sizeof(at_glyph_t));
    glyph->codepoint = codepoint;
    glyph->advance = advance;
    glyph->pitch = pitch;
    glyph->rows = face->size->metrics.y_ppem;
    glyph->pixel_array = calloc(glyph->rows * pitch, sizeof(uint8_t));

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

            int dst_byte_index = dst_y * pitch + (dst_x / 8);
            int dst_bit_index = dst_x % 8;

            int8_t src_bit_mask = (1 << (7 - src_bit_index));
            int8_t dst_bit_mask = (1 << (7 - dst_bit_index));

            if (unaligned_bitmap->buffer[src_byte_index] & src_bit_mask) {
                glyph->pixel_array[dst_byte_index] |= dst_bit_mask;
            }
        }
    }

    return glyph;
}

int pitch_32bit(int width) {
    return ((width + 31) / 32) * 4;
}

int floor_frac_pixel(long fractional_pixel_count) {
    return fractional_pixel_count >> 6;
}

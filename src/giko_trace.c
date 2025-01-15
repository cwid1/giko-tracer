#include "giko.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CMD_LEN 1024
#define PAGE_SIZE 4096
#define MAX_PATH_LEN 4096

typedef enum { LOW, MEDIUM, HIGH } fidelity_t;

typedef struct config {
    char charset_file[MAX_PATH_LEN];
    char image_file[MAX_PATH_LEN];
    char font_file[MAX_PATH_LEN];
    char output_file[MAX_PATH_LEN];
    int height;
    int base_encoding;
    sort_order_t glyph_map_order;
    float chunkiness;
    float accuracy;
    float denoise;
    fidelity_t fidelity;
    int negate;
} config_t;

uint8_t *magick_pipe(char *img_filepath);
giko_bitmap_t *get_bitmap(uint8_t *bmp_blob);
void print_codepoint_str(giko_codepoint_t *string);

// Helper functions
int linear(int x) { return x; }
int quadratic(int x) { return x * x; }
int cubic(int x) { return x * x * x; }
int pitch_32bit(int width) { return ((width + 31) / 32) * 4; }

int giko_trace(config_t config) {
    giko_codepoint_t *charset =
        giko_load_charset(config.charset_file, config.base_encoding);

    int (*fidelity_function)(int);
    if (config.fidelity == LOW) {
        fidelity_function = linear;
    } else if (config.fidelity == MEDIUM) {
        fidelity_function = quadratic;
    } else if (config.fidelity == HIGH) {
        fidelity_function = cubic;
    }

    uint8_t *bmp_blob = magick_pipe(config.image_file);
    if (!bmp_blob) {
        fprintf(stderr, "Error using image magick. Please make sure image "
                        "magick is installed on your system\n");
        return EXIT_FAILURE;
    }
    giko_bitmap_t *reference = get_bitmap(bmp_blob);

    if (!config.negate) {
        giko_negate_bitmap(reference);
    }

    int glyph_size = reference->height / config.height;
    if (glyph_size <= 0) {
        fprintf(
            stderr,
            "Error: --height must be less than height of reference image.\n");
        return EXIT_FAILURE;
    }

    giko_glyph_map_t *map = giko_new_glyph_map(
        config.font_file, charset, glyph_size, config.glyph_map_order);
    giko_codepoint_t *aa =
        giko_new_art_str(reference, map, 1 - config.chunkiness, config.accuracy,
                         config.denoise, fidelity_function);

    if (strlen(config.output_file) > 0) {
        giko_write_codepoint_str(aa, config.output_file);
    } else {
        print_codepoint_str(aa);
    }

    return EXIT_SUCCESS;
}

uint8_t *magick_pipe(char *img_filepath) {
    FILE *pipe;
    uint8_t buffer[PAGE_SIZE];
    int bytes_read;
    int blob_size = 0;
    char command[MAX_CMD_LEN];

    uint8_t *bmp_blob = NULL;

    snprintf(command, sizeof(command),
             "magick %s -threshold 50%% -type bilevel BMP:-", img_filepath);
    pipe = popen(command, "r");
    if (!pipe) {
        perror("popen");
        return NULL;
    }
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        bmp_blob = realloc(bmp_blob, blob_size + bytes_read);
        if (!bmp_blob) {
            perror("Error allocating memory");
            pclose(pipe);
            return NULL;
        }
        // Copy the buffer data into the allocated memory
        memcpy(bmp_blob + blob_size, buffer, bytes_read);
        blob_size += bytes_read;
    }

    pclose(pipe);
    return bmp_blob;
}

giko_bitmap_t *get_bitmap(uint8_t *bmp_blob) {
    int width;
    int height;
    memcpy(&width, bmp_blob + 18, sizeof(int));
    memcpy(&height, bmp_blob + 22, sizeof(int));

    uint32_t pixel_data_offset;
    memcpy(&pixel_data_offset, bmp_blob + 10, sizeof(uint32_t));

    int bitmap_size = pitch_32bit(width) * height;
    uint8_t *pixel_data = malloc(bitmap_size * sizeof(uint8_t));
    if (!pixel_data) {
        perror("Error allocating memory");
        return NULL;
    }
    memcpy(pixel_data, bmp_blob + pixel_data_offset, bitmap_size);

    giko_bitmap_t *bitmap = giko_new_bitmap(width, height, pixel_data);
    if (!bitmap)
        return NULL;
    giko_flip_bitmap(bitmap);

    return bitmap;
}

void print_codepoint_str(giko_codepoint_t *string) {
    // Print to stdout
    uint8_t utf8[4];
    int index = 0;
    giko_codepoint_t codepoint = string[0];

    while (codepoint != 0) {
        int length = giko_codepoint_to_utf8(utf8, codepoint);

        if (length > 0) {
            fwrite(utf8, 1, length, stdout);
        } else {
            fprintf(stderr, "Invalid codepoint: U+%04X\n", codepoint);
        }
        index++;
        codepoint = string[index];
    }
}

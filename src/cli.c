#include "giko_trace.c"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_HEIGHT 32
#define DEFAULT_BASE_ENCODING 10
#define DEFAULT_CHUNKINESS 0.5
#define DEFAULT_ACCURACY 0.5
#define DEFAULT_DENOISE 0.05
#define DEFAULT_SORT_ORDER NONE
#define DEFAULT_FIDELITY HIGH
#define DEFAULT_NEGATION 0
#define DEFAULT_VERBOSE 0

// Function prototypes
void parse_config_file(const char *conf_path, config_t *config);
void print_usage(const char *program_name);
void print_config(config_t config);

int main(int argc, char *argv[]) {
    config_t config = {"",
                       "",
                       "",
                       "",
                       DEFAULT_HEIGHT,
                       DEFAULT_BASE_ENCODING,
                       DEFAULT_SORT_ORDER,
                       DEFAULT_CHUNKINESS,
                       DEFAULT_ACCURACY,
                       DEFAULT_DENOISE,
                       DEFAULT_FIDELITY,
                       DEFAULT_NEGATION};
    char config_file[MAX_PATH_LEN] = "";
    int verbose = 0;

    // Long options for getopt_long
    static struct option long_options[] = {
        {"charset-file", required_argument, 0, 'c'},
        {"image-file", required_argument, 0, 'i'},
        {"font-file", required_argument, 0, 'f'},
        {"output", required_argument, 0, 'o'},
        {"conf", required_argument, 0, 'C'},
        {"height", required_argument, 0, 'H'},
        {"base-encoding", required_argument, 0, 'b'},
        {"glyph_map_order", required_argument, 0, 'g'},
        {"chunkiness", required_argument, 0, 'k'},
        {"accuracy", required_argument, 0, 'a'},
        {"denoise", required_argument, 0, 'd'},
        {"fidelity", required_argument, 0, 'F'},
        {"negate", no_argument, 0, 'n'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "c:i:f:o:C:H:b:s:g:k:a:d:F:nvh",
                              long_options, &option_index)) != -1) {
        switch (opt) {
        case 'C':
            strncpy(config_file, optarg, MAX_PATH_LEN - 1);
            // Load config file if specified
            if (strlen(config_file) > 0) {
                parse_config_file(config_file, &config);
            }
            break;
        case 'c':
            strncpy(config.charset_file, optarg, MAX_PATH_LEN - 1);
            break;
        case 'i':
            strncpy(config.image_file, optarg, MAX_PATH_LEN - 1);
            break;
        case 'f':
            strncpy(config.font_file, optarg, MAX_PATH_LEN - 1);
            break;
        case 'o':
            strncpy(config.output_file, optarg, MAX_PATH_LEN - 1);
            break;
        case 'H':
            config.height = atoi(optarg);
            if (config.height < 0) {
                fprintf(stderr, "Error: --height must be positive.\n");
                return EXIT_FAILURE;
            }
            break;
        case 'b':
            config.base_encoding = atoi(optarg);
            if (config.height < 0) {
                fprintf(stderr, "Error: --base-encoding must be positive.\n");
                return EXIT_FAILURE;
            }
            break;
        case 'g':
            if (strcmp(optarg, "NONE") == 0) {
                config.glyph_map_order = NONE;
            } else if (strcmp(optarg, "ASCENDING") == 0) {
                config.glyph_map_order = ASCENDING;
            } else if (strcmp(optarg, "DESCENDING") == 0) {
                config.glyph_map_order = DESCENDING;
            } else {
                fprintf(stderr, "Invalid value for --glyph_map_order. Use "
                                "NONE, ASCENDING, or DESCENDING.\n");
                return EXIT_FAILURE;
            }
            break;
        case 'k':
            config.chunkiness = atof(optarg);
            if (config.chunkiness < 0 || config.chunkiness > 1) {
                fprintf(stderr,
                        "Error: --chunkiness must be between 0 and 1.\n");
                return EXIT_FAILURE;
            }
            break;
        case 'a':
            config.accuracy = atof(optarg);
            if (config.accuracy < 0 || config.accuracy > 1) {
                fprintf(stderr, "Error: --accuracy must be between 0 and 1.\n");
                return EXIT_FAILURE;
            }
            break;
        case 'd':
            config.denoise = atof(optarg);
            if (config.denoise < 0 || config.denoise > 1) {
                fprintf(stderr, "Error: --denoise must be between 0 and 1.\n");
                return EXIT_FAILURE;
            }
            break;
        case 'F':
            if (strcmp(optarg, "LOW") == 0) {
                config.fidelity = LOW;
            } else if (strcmp(optarg, "MEDIUM") == 0) {
                config.fidelity = MEDIUM;
            } else if (strcmp(optarg, "HIGH") == 0) {
                config.fidelity = HIGH;
            } else {
                fprintf(stderr, "Invalid value for --fidelity. Use LOW, "
                                "MEDIUM, or HIGH.\n");
                return EXIT_FAILURE;
            }
            break;
        case 'n':
            config.negate = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    // Ensure required arguments are provided
    if (strlen(config.charset_file) == 0 || strlen(config.image_file) == 0 ||
        strlen(config.font_file) == 0) {
        fprintf(stderr, "Error: charset file, image file, and font file must "
                        "be specified.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (verbose) {
        print_config(config);
    }

    giko_trace(config);

    return EXIT_SUCCESS;
}

void parse_config_file(const char *conf_path, config_t *config) {
    FILE *file = fopen(conf_path, "r");
    if (!file) {
        perror(conf_path);
        exit(EXIT_FAILURE);
    }

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char key[128], value[MAX_PATH_LEN];
        if (sscanf(line, "%127[^=]=%4095s", key, value) == 2) {
            if (strcmp(key, "charset_file") == 0) {
                strncpy(config->charset_file, value, MAX_PATH_LEN - 1);
            } else if (strcmp(key, "image_file") == 0) {
                strncpy(config->image_file, value, MAX_PATH_LEN - 1);
            } else if (strcmp(key, "font_file") == 0) {
                strncpy(config->font_file, value, MAX_PATH_LEN - 1);
            } else if (strcmp(key, "output_file") == 0) {
                strncpy(config->output_file, value, MAX_PATH_LEN - 1);
            } else if (strcmp(key, "height") == 0) {
                config->height = atoi(value);
            } else if (strcmp(key, "base_encoding") == 0) {
                config->base_encoding = atoi(value);
            } else if (strcmp(key, "glyph_map_order") == 0) {
                if (strcmp(value, "NONE") == 0) {
                    config->glyph_map_order = NONE;
                } else if (strcmp(value, "ASCENDING") == 0) {
                    config->glyph_map_order = ASCENDING;
                } else if (strcmp(value, "DESCENDING") == 0) {
                    config->glyph_map_order = DESCENDING;
                }
            } else if (strcmp(key, "chunkiness") == 0) {
                config->chunkiness = atof(value);
            } else if (strcmp(key, "accuracy") == 0) {
                config->accuracy = atof(value);
            } else if (strcmp(key, "denoise") == 0) {
                config->denoise = atof(value);
            } else if (strcmp(key, "fidelity") == 0) {
                if (strcmp(value, "LOW") == 0) {
                    config->fidelity = LOW;
                } else if (strcmp(value, "MEDIUM") == 0) {
                    config->fidelity = MEDIUM;
                } else if (strcmp(value, "HIGH") == 0) {
                    config->fidelity = HIGH;
                }
            } else if (strcmp(key, "negate") == 0) {
                if (strcmp(value, "true")) {
                    config->negate = 1;
                } else {
                    config->negate = 0;
                }
            }
        }
    }

    fclose(file);
}

void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Options:\n");
    printf("  -h, --help                    Print this message\n");
    printf("  -c, --charset-file PATH       Path to the charset file\n");
    printf("  -i, --image-file PATH         Path to the image file\n");
    printf("  -f, --font-file PATH          Path to the font file\n");
    printf("  -o, --output PATH             Path to the output file (default: "
           "stdout)\n");
    printf("  -C, --conf PATH               Path to the config file\n");
    printf("  -H, --height NUMBER           Height of the ASCII art (default: "
           "32)\n");
    printf("  -b, --base-encoding NUMBER    Base encoding of the charset "
           "codepoints (default: 64)\n");
    printf("  -g, --glyph_map_order ENUM    Glyph map order: NONE, ASCENDING, "
           "DESCENDING (default: NONE)\n");
    printf("  -k, --chunkiness FLOAT        Chunkiness factor (0 to 1, "
           "default: 0.5)\n");
    printf("  -a, --accuracy FLOAT          Accuracy factor (0 to 1, default: "
           "0.5)\n");
    printf("  -d, --denoise FLOAT           Denoise factor (0 to 1, default: "
           "0.05)\n");
    printf("  -F, --fidelity ENUM           Fidelity: LOW, MEDIUM, HIGH "
           "(default: MEDIUM)\n");
    printf("  -n, --negate                  Negate (invert) colours of image"
           "of the image\n");
    printf("  -v, --verbose                 Print argument list\n");
}

void print_config(config_t config) {
    printf("Charset file: %s\n", config.charset_file);
    printf("Image file: %s\n", config.image_file);
    printf("Font file: %s\n", config.font_file);
    printf("Output file: %s\n",
           (strlen(config.output_file) > 0) ? config.output_file : "stdout");
    printf("Height: %d\n", config.height);
    printf("Base encoding: %d\n", config.base_encoding);
    printf("Glyph map order: %s\n", (config.glyph_map_order == NONE) ? "NONE"
                                    : (config.glyph_map_order == ASCENDING)
                                        ? "ASCENDING"
                                        : "DESCENDING");
    printf("Chunkiness: %.2f\n", config.chunkiness);
    printf("Accuracy: %.2f\n", config.accuracy);
    printf("Denoise: %.2f\n", config.denoise);
    printf("Fidelity: %s\n", (config.fidelity == LOW)      ? "LOW"
                             : (config.fidelity == MEDIUM) ? "MEDIUM"
                                                           : "HIGH");
    printf("Negate: %s\n", (config.negate) ? "true" : "false");
}

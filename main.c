#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include "bch.h"

#define PAGE_SIZE 4352
#define BLOCK_SIZE 528
#define DATA_SIZE 514
#define ECC_SIZE 14
#define SPARE_SIZE 128
#define BLOCKS_PER_PAGE 8
#define MAX_ERRORS 8  // t=8

int main(int argc, char *argv[]) {
    bool verbose = false;
    long skip_pages = 0;  // Default to skipping 0 pages
    const char *command = NULL;
    const char *input_file = NULL;
    const char *output_file = NULL;

    // Parse arguments
    int arg_index = 1;
    while (arg_index < argc && argv[arg_index][0] == '-') {
        if (strcmp(argv[arg_index], "-v") == 0) {
            verbose = true;
            arg_index++;
        } else if (strcmp(argv[arg_index], "-skip") == 0) {
            if (arg_index + 1 >= argc) {
                fprintf(stderr, "Error: -skip requires a number\n");
                return 1;
            }
            char *endptr;
            skip_pages = strtol(argv[arg_index + 1], &endptr, 10);
            if (*endptr != '\0' || skip_pages < 0) {
                fprintf(stderr, "Error: Invalid skip value\n");
                return 1;
            }
            arg_index += 2;
        } else {
            fprintf(stderr, "Error: Unknown option %s\n", argv[arg_index]);
            return 1;
        }
    }

    if (argc < arg_index + 2) {
        fprintf(stderr, "Usage: %s [-v] [-skip N] <command> <input_file> [output_file]\n", argv[0]);
        fprintf(stderr, "Commands: check, fixdata, fixecc\n");
        return 1;
    }

    command = argv[arg_index];
    input_file = argv[arg_index + 1];
    output_file = (argc > arg_index + 2) ? argv[arg_index + 2] : NULL;

    bool do_check = strcmp(command, "check") == 0;
    bool do_fixdata = strcmp(command, "fixdata") == 0;
    bool do_fixecc = strcmp(command, "fixecc") == 0;

    if (!do_check && !do_fixdata && !do_fixecc) {
        fprintf(stderr, "Invalid command: %s\n", command);
        return 1;
    }

    if ((do_fixdata || do_fixecc) && !output_file) {
        fprintf(stderr, "Output file required for %s\n", command);
        return 1;
    }

    struct bch_control *bch = bch_init(14, 8, 0x402b, false);
    if (!bch) {
        fprintf(stderr, "Failed to initialize BCH\n");
        return 1;
    }

    FILE *fin = fopen(input_file, "rb");
    if (!fin) {
        fprintf(stderr, "Failed to open input file: %s\n", strerror(errno));
        bch_free(bch);
        return 1;
    }

    FILE *fout = NULL;
    if (output_file) {
        fout = fopen(output_file, "wb");
        if (!fout) {
            fprintf(stderr, "Failed to open output file: %s\n", strerror(errno));
            fclose(fin);
            bch_free(bch);
            return 1;
        }
    }

    // Skip initial pages
    if (skip_pages > 0) {
        uint8_t page[PAGE_SIZE];
        for (long i = 0; i < skip_pages; i++) {
            size_t read = fread(page, 1, PAGE_SIZE, fin);
            if (read != PAGE_SIZE) {
                fprintf(stderr, "Failed to read page %ld for skipping\n", i + 1);
                fclose(fin);
                if (fout) fclose(fout);
                bch_free(bch);
                return 1;
            }
            if (fout && (do_fixdata || do_fixecc)) {
                fwrite(page, 1, PAGE_SIZE, fout);
            }
        }
    }

    uint8_t page[PAGE_SIZE];
    uint8_t block[BLOCK_SIZE];
    uint8_t inverted_data[DATA_SIZE];
    uint8_t inverted_ecc[ECC_SIZE];
    unsigned int errloc[MAX_ERRORS];
    size_t pages_read = 0;
    size_t erroneous_blocks = 0;
    size_t erroneous_bits = 0;
    size_t uncorrectable_blocks = 0;

    while (true) {
        size_t read = fread(page, 1, PAGE_SIZE, fin);
        if (read == 0) break;  // EOF
        if (read != PAGE_SIZE) {
            fprintf(stderr, "Incomplete page at end of file, ignoring\n");
            break;
        }

        pages_read++;
        size_t page_offset = (skip_pages + pages_read - 1) * PAGE_SIZE;

        // Process each block in the page
        for (int b = 0; b < BLOCKS_PER_PAGE; b++) {
            size_t block_offset = page_offset + b * BLOCK_SIZE;
            memcpy(block, page + b * BLOCK_SIZE, BLOCK_SIZE);

            uint8_t *data = block;
            uint8_t *recv_ecc = block + DATA_SIZE;

            // Invert data and ECC for processing
            for (size_t i = 0; i < DATA_SIZE; i++) {
                inverted_data[i] = ~data[i];
            }
            for (size_t i = 0; i < ECC_SIZE; i++) {
                inverted_ecc[i] = ~recv_ecc[i];
            }

            // Decode block to check for errors
            int num_err = bch_decode(bch, inverted_data, DATA_SIZE, inverted_ecc, NULL, NULL, errloc);

            if (num_err != 0) {
                erroneous_blocks++;
                if (num_err > 0) {
                    erroneous_bits += num_err;
                } else {
                    uncorrectable_blocks++;
                }
            }

            if (do_check) {
                if (verbose && num_err != 0) {
                    if (num_err > 0) {
                        fprintf(stderr, "Correctable %d bit errors at offset 0x%zx (page %zu, block %d)\n",
                                num_err, block_offset, pages_read + skip_pages, b);
                    } else {
                        fprintf(stderr, "Uncorrectable error at offset 0x%zx (page %zu, block %d)\n",
                                block_offset, pages_read + skip_pages, b);
                    }
                }
            } else if (do_fixdata) {
                if (num_err >= 0) {
                    // Correct errors in inverted data
                    for (int i = 0; i < num_err; i++) {
                        unsigned int loc = errloc[i];
                        if (loc < DATA_SIZE * 8) {
                            size_t byte = loc / 8;
                            size_t bit = loc % 8;
                            inverted_data[byte] ^= (1 << bit);
                        }
                        // Ignore errors in ECC for fixdata
                    }
                    // Write back corrected (re-inverted) data + original ECC
                    for (size_t i = 0; i < DATA_SIZE; i++) {
                        data[i] = ~inverted_data[i];
                    }
                    fwrite(block, 1, BLOCK_SIZE, fout);
                    if (verbose && num_err > 0) {
                        fprintf(stderr, "Corrected %d bit errors at offset 0x%zx (page %zu, block %d)\n",
                                num_err, block_offset, pages_read + skip_pages, b);
                    }
                } else {
                    // Uncorrectable, write original block as is
                    if (verbose) {
                        fprintf(stderr, "Uncorrectable error at offset 0x%zx (page %zu, block %d), leaving as is\n",
                                block_offset, pages_read + skip_pages, b);
                    }
                    fwrite(block, 1, BLOCK_SIZE, fout);
                }
            } else if (do_fixecc) {
                if (num_err < 0) {
                    // Check if ECC is all 0x00
                    bool all_00 = true;
                    for (size_t i = 0; i < ECC_SIZE; i++) {
                        if (recv_ecc[i] != 0x00) {
                            all_00 = false;
                            break;
                        }
                    }
                    if (all_00) {
                        // Recalculate ECC for blocks with all-0x00 ECC
                        uint8_t new_ecc[ECC_SIZE];
                        memset(new_ecc, 0, ECC_SIZE);
                        bch_encode(bch, inverted_data, DATA_SIZE, new_ecc);
                        // Write original data + inverted new ECC
                        fwrite(data, 1, DATA_SIZE, fout);
                        for (size_t i = 0; i < ECC_SIZE; i++) {
                            new_ecc[i] = ~new_ecc[i];
                        }
                        fwrite(new_ecc, 1, ECC_SIZE, fout);
                        if (verbose) {
                            fprintf(stderr, "Recalculated ECC (was all 0x00) at offset 0x%zx (page %zu, block %d)\n",
                                    block_offset, pages_read + skip_pages, b);
                        }
                    } else {
                        // Uncorrectable and ECC not all 0x00, write original block unchanged
                        if (verbose) {
                            fprintf(stderr, "Uncorrectable error at offset 0x%zx (page %zu, block %d), writing unchanged\n",
                                    block_offset, pages_read + skip_pages, b);
                        }
                        fwrite(block, 1, BLOCK_SIZE, fout);
                    }
                } else {
                    // Compute new ECC on inverted data
                    uint8_t new_ecc[ECC_SIZE];
                    memset(new_ecc, 0, ECC_SIZE);
                    bch_encode(bch, inverted_data, DATA_SIZE, new_ecc);
                    // Write original data + inverted new ECC
                    fwrite(data, 1, DATA_SIZE, fout);
                    for (size_t i = 0; i < ECC_SIZE; i++) {
                        new_ecc[i] = ~new_ecc[i];
                    }
                    fwrite(new_ecc, 1, ECC_SIZE, fout);
                    if (verbose && num_err > 0) {
                        fprintf(stderr, "Corrected %d bit errors in ECC at offset 0x%zx (page %zu, block %d)\n",
                                num_err, block_offset, pages_read + skip_pages, b);
                    }
                }
            }
        }

        // Write spare area (128 bytes) if output file is used
        if (fout) {
            fwrite(page + BLOCKS_PER_PAGE * BLOCK_SIZE, 1, SPARE_SIZE, fout);
        }
    }

    printf("Number of erroneous blocks: %zu\n", erroneous_blocks);
    printf("Number of erroneous bits: %zu\n", erroneous_bits);
    printf("Number of uncorrectable blocks: %zu\n", uncorrectable_blocks);

    fclose(fin);
    if (fout) fclose(fout);
    bch_free(bch);

    return 0;
}
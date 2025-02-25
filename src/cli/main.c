/*
 *  main.c
 *  neoaa
 *
 *  Created by Snoolie Keffaber on 2024/06/29.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>
#include "../lib/libNeoAppleArchive/libNeoAppleArchive.h"
#include "../lib/build/lzfse/include/lzfse.h"

#define OPTSTR "i:o:a:p:hv"

typedef enum {
    NEOAA_CMD_ARCHIVE,
    NEOAA_CMD_EXTRACT,
    NEOAA_CMD_LIST,
    NEOAA_CMD_ADD,
    NEOAA_CMD_WRAP,
    NEOAA_CMD_UNWRAP,
    NEOAA_CMD_VERSION,
} NeoAACommand;

typedef enum {
    NEOAA_COMPRESS_LZFSE,
    NEOAA_COMPRESS_RAW,
    NEOAA_COMPRESS_ZLIB,
} NeoAACompression;

extern char *optarg;

void show_help(void) {
    printf("Usage: neoaa command <options>\n\n");
    printf("Commands:\n\n");
    /* printf(" archive: archive the contents of a directory.\n"); */
    printf(" extract: extract files from an archive.\n");
    printf(" list: list the contents of an archive.\n");
    printf(" wrap: archive a singular file.\n");
    printf(" unwrap: extract a singular file from an archive.\n");
    printf(" version: display version of aa\n");
    printf("\n");
    printf("Options:\n\n");
    printf(" -i: path to the input file or directory.\n");
    printf(" -o: path to the output file or directory.\n");
    printf(" -a: algorithm for compression, lzfse (default), zlib, raw (no compression).\n");
    printf(" -p: specify path of file in project to unwrap or add.\n");
    printf(" -f: path of file to add to the .aar specified in -i.\n");
    printf(" -h: this ;-)\n");
    printf("\n");
}

void list_neo_aa_files(const char *inputPath) {
    NeoAAArchiveGeneric genericArchive = neo_aa_archive_generic_from_path(inputPath);
    if (!genericArchive) {
        fprintf(stderr,"Not enough free memory to list files\n");
        return;
    }
    NeoAAArchivePlain archive = genericArchive->raw;
    for (int i = 0; i < archive->itemCount; i++) {
        /*
         * We loop through all items to find the PAT field key.
         * The PAT field key will be what path the item is in the
         * archive. This also includes symlinks.
         */
        NeoAAArchiveItem item = archive->items[i];
        NeoAAHeader header = item->header;
        int index = neo_aa_header_get_field_key_index(header, NEO_AA_FIELD_C("PAT"));
        if (index == -1) {
            continue;
        }
        /* If index is not -1, then header has PAT field key */
        char *patStr = neo_aa_header_get_field_key_string(header, index);
        if (patStr) {
            printf("Could not get PAT entry in header\n");
            continue;
        }
        printf("%s\n",patStr);
        free(patStr);
    }
}

void add_file_in_neo_aa(const char *inputPath, const char *outputPath, const char *addPath, NeoAACompression compress) {
    NeoAAHeader header = neo_aa_header_create();
    if (!header) {
        fprintf(stderr,"Failed to create header\n");
        return;
    }
    char *fileName = basename((char *)addPath);
    /* Declare our file as, well, a file */
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("TYP"), 1, 'F');
    /* Declare our PAT to be our file name */
    neo_aa_header_add_field_string(header, NEO_AA_FIELD_C("PAT"), strlen(fileName), fileName);
    /* Set other field keys */
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("UID"), 2, 0x1F5);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("GID"), 1, 0x14);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("MOD"), 2, 0x1ED);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("FLG"), 1, 0);
    /* Crete the NeoAAArchiveItem item */
    NeoAAArchiveItem item = neo_aa_archive_item_create_with_header(header);
    if (!item) {
        neo_aa_header_destroy(header);
        fprintf(stderr,"Failed to create item\n");
        return;
    }
    FILE *fp = fopen(addPath, "r");
    if (!fp) {
        neo_aa_archive_item_destroy(item);
        fprintf(stderr,"Failed to open input path\n");
        return;
    }
    fseek(fp, 0, SEEK_END);
    size_t binarySize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    /* allocate our uncompressed data */
    uint8_t *data = malloc(binarySize);
    if (!data) {
        fclose(fp);
        neo_aa_archive_item_destroy(item);
        fprintf(stderr,"Not enough memory to allocate file into memory\n");
        return;
    }
    memset(data, 0, binarySize);
    ssize_t bytesRead = fread(data, binarySize, 1, fp);
    fclose(fp);
    if (bytesRead < binarySize) {
        neo_aa_archive_item_destroy(item);
        fprintf(stderr,"Failed to read the entire file\n");
        return;
    }
    
    /* Handle other than RAW later */
    if (binarySize < USHRT_MAX) {
        neo_aa_header_set_field_blob(header, NEO_AA_FIELD_C("DAT"), 2, binarySize);
    } else if (binarySize < UINT32_MAX) {
        neo_aa_header_set_field_blob(header, NEO_AA_FIELD_C("DAT"), 4, binarySize);
    } else {
        neo_aa_header_set_field_blob(header, NEO_AA_FIELD_C("DAT"), 8, binarySize);
    }
    neo_aa_archive_item_add_blob_data(item, (char *)data, binarySize);
    free(data);
    
    /* Make NeoAAArchivePlain from inputPath */
    NeoAAArchiveGeneric plainInputArchive = neo_aa_archive_generic_from_path(inputPath);
    if (!plainInputArchive) {
        neo_aa_archive_item_destroy(item);
        fprintf(stderr,"Not enough memory to make NeoAAArchiveGeneric\n");
        return;
    }
    NeoAAArchivePlain rawInput = plainInputArchive->raw;
    /* VLAs are ugly but eh */
    NeoAAArchiveItem itemList[rawInput->itemCount + 1];
    memcpy(itemList, rawInput->items, rawInput->itemCount);
    itemList[rawInput->itemCount] = item;
    free(plainInputArchive);
    NeoAAArchivePlain archive = neo_aa_archive_plain_create_with_items(itemList, 1);
    neo_aa_archive_item_destroy(item);
    neo_aa_archive_plain_destroy(rawInput);
    if (!archive) {
        fprintf(stderr,"Failed to create NeoAAArchivePlain\n");
        return;
    }
    if (NEOAA_COMPRESS_LZFSE == compress) {
        neo_aa_archive_plain_compress_write_path(archive, 0x801, outputPath);
        return;
    }
    neo_aa_archive_plain_write_path(archive, outputPath);
}

void wrap_file_in_neo_aa(const char *inputPath, const char *outputPath, NeoAACompression compress) {
    NeoAAHeader header = neo_aa_header_create();
    if (!header) {
        fprintf(stderr,"Failed to create header\n");
        return;
    }
    char *fileName = basename((char *)inputPath);
    /* Declare our file as, well, a file */
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("TYP"), 1, 'F');
    /* Declare our PAT to be our file name */
    neo_aa_header_add_field_string(header, NEO_AA_FIELD_C("PAT"), strlen(fileName), fileName);
    /* Set other field keys */
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("UID"), 2, 0x1F5);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("GID"), 1, 0x14);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("MOD"), 2, 0x1ED);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("FLG"), 1, 0);
    /* Crete the NeoAAArchiveItem item */
    NeoAAArchiveItem item = neo_aa_archive_item_create_with_header(header);
    if (!item) {
        neo_aa_header_destroy(header);
        fprintf(stderr,"Failed to create item\n");
        return;
    }
    FILE *fp = fopen(inputPath, "r");
    if (!fp) {
        neo_aa_archive_item_destroy(item);
        fprintf(stderr,"Failed to open input path\n");
        return;
    }
    fseek(fp, 0, SEEK_END);
    size_t binarySize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    /* allocate our uncompressed data */
    uint8_t *data = malloc(binarySize);
    if (!data) {
        fclose(fp);
        neo_aa_archive_item_destroy(item);
        fprintf(stderr,"Not enough memory to allocate file into memory\n");
        return;
    }
    memset(data, 0, binarySize);
    ssize_t bytesRead = fread(data, 1, binarySize, fp);
    fclose(fp);
    if (bytesRead < binarySize) {
        neo_aa_archive_item_destroy(item);
        fprintf(stderr,"Failed to read the entire file\n");
        return;
    }
    
    /* Handle other than RAW later */
    if (binarySize < USHRT_MAX) {
        neo_aa_header_set_field_blob(header, NEO_AA_FIELD_C("DAT"), 2, binarySize);
    } else if (binarySize < UINT32_MAX) {
        neo_aa_header_set_field_blob(header, NEO_AA_FIELD_C("DAT"), 4, binarySize);
    } else {
        neo_aa_header_set_field_blob(header, NEO_AA_FIELD_C("DAT"), 8, binarySize);
    }
    neo_aa_archive_item_add_blob_data(item, (char *)data, binarySize);
    free(data);
    NeoAAArchiveItem *itemList = &item;
    NeoAAArchivePlain archive = neo_aa_archive_plain_create_with_items(itemList, 1);
    neo_aa_archive_item_destroy(item);
    if (!archive) {
        fprintf(stderr,"Failed to create NeoAAArchivePlain\n");
        return;
    }
    if (NEOAA_COMPRESS_LZFSE == compress) {
        neo_aa_archive_plain_compress_write_path(archive, 0x801, outputPath);
        return;
    }
    neo_aa_archive_plain_write_path(archive, outputPath);
}

void unwrap_file_out_of_neo_aa(const char *inputPath, const char *outputPath, char *pathString) {
    NeoAAArchiveGeneric genericArchive = neo_aa_archive_generic_from_path(inputPath);
    if (!genericArchive) {
        fprintf(stderr,"Not enough free memory to list files\n");
        return;
    }
    NeoAAArchivePlain archive = genericArchive->raw;
    for (int i = 0; i < archive->itemCount; i++) {
        /*
         * We loop through all items to find the PAT field key.
         * The PAT field key will be what path the item is in the
         * archive. This also includes symlinks.
         */
        NeoAAArchiveItem item = archive->items[i];
        NeoAAHeader header = item->header;
        int index = neo_aa_header_get_field_key_index(header, NEO_AA_FIELD_C("PAT"));
        if (index == -1) {
            continue;
        }
        /* If index is not -1, then header has PAT field key */
        char *patStr = neo_aa_header_get_field_key_string(header, index);
        if (!patStr) {
            printf("Could not get PAT entry in header\n");
            continue;
        }
        if (strncmp(pathString,patStr,strlen(pathString)) == 0) {
            free(patStr);
            /* Unwrap file */
            FILE *fp = fopen(outputPath, "w");
            if (!fp) {
                fprintf(stderr,"Failed to open outputPath.\n");
                return;
            }
            fwrite(item->encodedBlobData, item->encodedBlobDataSize, 1, fp);
            fclose(fp);
            return;
        }
        free(patStr);
    }
    printf("Could not find file at the specified path in the project.\n");
}

int main(int argc, const char * argv[]) {
    if (argc < 2) {
        show_help();
        return 0;
    }
    /* Parse commands */
    NeoAACommand neoaaCommand;
    const char *commandString = argv[1];
    if (strncmp(commandString, "archive", 7) == 0) {
        neoaaCommand = NEOAA_CMD_ARCHIVE;
    } else if (strncmp(commandString, "extract", 7) == 0) {
        neoaaCommand = NEOAA_CMD_EXTRACT;
    } else if (strncmp(commandString, "list", 4) == 0) {
        neoaaCommand = NEOAA_CMD_LIST;
    } else if (strncmp(commandString, "add", 3) == 0) {
        neoaaCommand = NEOAA_CMD_ADD;
    } else if (strncmp(commandString, "wrap", 4) == 0) {
        neoaaCommand = NEOAA_CMD_WRAP;
    } else if (strncmp(commandString, "unwrap", 6) == 0) {
        neoaaCommand = NEOAA_CMD_UNWRAP;
    } else if (strncmp(commandString, "version", 7) == 0) {
        neoaaCommand = NEOAA_CMD_VERSION;
    } else {
        printf("Invalid command.\n");
        show_help();
        return 0;
    }
    /* Hack to get getopt() to skip the command in argv */
    argv++;
    argc--;

    char *inputPath = NULL;
    char *outputPath = NULL;
    char *algorithmString = NULL;
    char *pathSpecifierString = NULL;
    char *fileAddString = NULL;
    
    /* Parse args */
    int opt;
    while ((opt = getopt(argc, (char* const *)argv, OPTSTR)) != EOF) {
        if (opt == 'i') {
            inputPath = optarg;
        } else if (opt == 'o') {
            outputPath = optarg;
        } else if (opt == 'a') {
            algorithmString = optarg;
        } else if (opt == 'p') {
            pathSpecifierString = optarg;
        } else if (opt == 'f') {
            fileAddString = optarg;
        } else if (opt == 'h') {
            /* Show help */
            show_help();
            return 0;
        }
    }
    
    NeoAACompression compress;
    
    /* Parse algorithmString */
    if (algorithmString) {
        if (strncmp(algorithmString, "raw", 3)) {
            compress = NEOAA_COMPRESS_RAW;
        } else if (strncmp(algorithmString, "zlib", 4)) {
            compress = NEOAA_COMPRESS_ZLIB;
        } else if (strncmp(algorithmString, "lzfse", 5)) {
            compress = NEOAA_COMPRESS_LZFSE;
        } else {
            /* Default compression is LZFSE */
            compress = NEOAA_COMPRESS_LZFSE;
        }
    } else {
        /* Default compression is LZFSE */
        compress = NEOAA_COMPRESS_LZFSE;
    }

    /* Test CLI tool */
    neo_aa_archive_item_destroy(neo_aa_archive_item_create_with_header(neo_aa_header_create()));
    
    /* NEOAA_CMD_VERSION is the only command where inputPath is not needed */
    if (NEOAA_CMD_VERSION == neoaaCommand) {
        printf("Pre-1.0 (Unfinished)\n");
        return 0;
    }
    if (!inputPath) {
        printf("No -i specified.\n");
        show_help();
    }
    if (NEOAA_CMD_LIST == neoaaCommand) {
        list_neo_aa_files(inputPath);
    } else if (NEOAA_CMD_WRAP == neoaaCommand) {
        if (!outputPath) {
            printf("No -o specified.\n");
            return 0;
        }
        wrap_file_in_neo_aa(inputPath, outputPath, compress);
    } else if (NEOAA_CMD_UNWRAP == neoaaCommand) {
        if (!outputPath) {
            printf("No -o specified.\n");
            return 0;
        }
        if (!pathSpecifierString) {
            printf("No -p specified.\n");
            return 0;
        }
        unwrap_file_out_of_neo_aa(inputPath, outputPath, pathSpecifierString);
    } else if (NEOAA_CMD_ADD == neoaaCommand) {
        if (!outputPath) {
            printf("No -o specified.\n");
            return 0;
        }
        if (!fileAddString) {
            printf("No -f specified.\n");
            return 0;
        }
        add_file_in_neo_aa(inputPath, outputPath, fileAddString, compress);
    } else if (NEOAA_CMD_EXTRACT == neoaaCommand) {
        if (!outputPath) {
            printf("No -o specified.\n");
            return 0;
        }
        neo_aa_extract_aar_to_path(inputPath, outputPath);
    }
    return 0;
}

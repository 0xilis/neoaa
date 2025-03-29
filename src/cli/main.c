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
#include <libNeoAppleArchive.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <lzfse.h>

#if !(defined(_WIN32) || defined(WIN32))
#include <sys/types.h>
#endif

#define OPTSTR "i:o:a:p:f:hv"

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
    printf(" archive: archive the contents of a directory.\n");
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
    printf(" -p: specify path of file in project to unwrap.\n");
    /* printf(" -f: path of file to add to the .aar specified in -i.\n"); */
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
        if (!patStr) {
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
    neo_aa_header_set_field_string(header, NEO_AA_FIELD_C("PAT"), strlen(fileName), fileName);
    /* Set other field keys */
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("UID"), 2, 0x1F5);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("GID"), 1, 0x14);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("MOD"), 2, 0x1ED);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("FLG"), 1, 0);
    /* Crete the NeoAAArchiveItem item */
    NeoAAArchiveItem item = neo_aa_archive_item_create_with_header(header);
    if (!item) {
        neo_aa_header_destroy_nozero(header);
        fprintf(stderr,"Failed to create item\n");
        return;
    }
    FILE *fp = fopen(addPath, "rb");
    if (!fp) {
        neo_aa_archive_item_destroy_nozero(item);
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
        neo_aa_archive_item_destroy_nozero(item);
        fprintf(stderr,"Not enough memory to allocate file into memory\n");
        return;
    }
    ssize_t bytesRead = fread(data, binarySize, 1, fp);
    fclose(fp);
    if (bytesRead < binarySize) {
        neo_aa_archive_item_destroy_nozero(item);
        fprintf(stderr,"Failed to read the entire file\n");
        return;
    }
    
    /* Handle other than RAW later */
    neo_aa_header_set_field_blob(header, NEO_AA_FIELD_C("DAT"), 0, binarySize);
    neo_aa_archive_item_add_blob_data(item, (char *)data, binarySize);
    free(data);
    
    /* Make NeoAAArchivePlain from inputPath */
    NeoAAArchiveGeneric plainInputArchive = neo_aa_archive_generic_from_path(inputPath);
    if (!plainInputArchive) {
        neo_aa_archive_item_destroy_nozero(item);
        fprintf(stderr,"Not enough memory to make NeoAAArchiveGeneric\n");
        return;
    }
    NeoAAArchivePlain rawInput = plainInputArchive->raw;
    /* VLAs are ugly but eh */
    NeoAAArchiveItem itemList[rawInput->itemCount + 1];
    memcpy(itemList, rawInput->items, rawInput->itemCount);
    itemList[rawInput->itemCount] = item;
    free(plainInputArchive);
    NeoAAArchivePlain archive = neo_aa_archive_plain_create_with_items_nocopy(itemList, rawInput->itemCount + 1);
    neo_aa_archive_plain_destroy_nozero(rawInput);
    if (!archive) {
        fprintf(stderr,"Failed to create NeoAAArchivePlain\n");
        return;
    }
    if (NEOAA_COMPRESS_LZFSE == compress) {
        neo_aa_archive_plain_compress_write_path(archive, NEOAA_COMPRESS_LZFSE, outputPath);
        return;
    }
    neo_aa_archive_plain_write_path(archive, outputPath);
    neo_aa_archive_plain_destroy_nozero(archive);
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
    neo_aa_header_set_field_string(header, NEO_AA_FIELD_C("PAT"), strlen(fileName), fileName);
    /* Set other field keys */
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("UID"), 2, 0x1F5);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("GID"), 1, 0x14);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("MOD"), 2, 0x1ED);
    neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("FLG"), 1, 0);
    /* Crete the NeoAAArchiveItem item */
    NeoAAArchiveItem item = neo_aa_archive_item_create_with_header(header);
    if (!item) {
        neo_aa_header_destroy_nozero(header);
        fprintf(stderr,"Failed to create item\n");
        return;
    }
    FILE *fp = fopen(inputPath, "r");
    if (!fp) {
        neo_aa_archive_item_destroy_nozero(item);
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
        neo_aa_archive_item_destroy_nozero(item);
        fprintf(stderr,"Not enough memory to allocate file into memory\n");
        return;
    }
    ssize_t bytesRead = fread(data, 1, binarySize, fp);
    fclose(fp);
    if (bytesRead < binarySize) {
        neo_aa_archive_item_destroy_nozero(item);
        fprintf(stderr,"Failed to read the entire file\n");
        return;
    }
    
    /* Handle other than RAW later */
    neo_aa_header_set_field_blob(header, NEO_AA_FIELD_C("DAT"), 0, binarySize);
    neo_aa_archive_item_add_blob_data(item, (char *)data, binarySize);
    free(data);
    NeoAAArchiveItem *itemList = &item;
    NeoAAArchivePlain archive = neo_aa_archive_plain_create_with_items_nocopy(itemList, 1);
    if (!archive) {
        fprintf(stderr,"Failed to create NeoAAArchivePlain\n");
        return;
    }
    if (NEOAA_COMPRESS_LZFSE == compress) {
        neo_aa_archive_plain_compress_write_path(archive, NEOAA_COMPRESS_LZFSE, outputPath);
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

/* TODO: Experimental */
/* Recursively archive directory contents */
static int add_directory_contents_to_archive(const char *dirPath, NeoAAArchiveItem *items, 
                                          size_t *itemsCount, size_t *itemsMalloc,
                                          const char *basePath) {
    DIR *dir = opendir(dirPath);
    if (!dir) {
        fprintf(stderr, "Failed to open directory: %s\n", dirPath);
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;  /* Skip "." and ".." */
        }

        char fullPath[1024];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);

        /* Build relative path for PAT field */
        char relativePath[2048];
        if (basePath && basePath[0] != '\0') {
            snprintf(relativePath, sizeof(relativePath), "%s/%s", basePath, entry->d_name);
        } else {
            strncpy(relativePath, entry->d_name, sizeof(relativePath));
        }

        /* Check if we need to grow our items array */
        if (*itemsCount == *itemsMalloc) {
            *itemsMalloc *= 2;
            NeoAAArchiveItem *newItems = (NeoAAArchiveItem *)realloc(items, sizeof(NeoAAArchiveItem) * (*itemsMalloc));
            if (!newItems) {
                fprintf(stderr, "Failed to realloc items array\n");
                closedir(dir);
                return -2;
            }
            items = newItems;
        }

        struct stat fileStat;
        if (lstat(fullPath, &fileStat) < 0) {
            perror("Failed to get file info");
            continue;
        }

        NeoAAHeader header = neo_aa_header_create();
        if (!header) {
            fprintf(stderr, "Failed to create header for %s\n", fullPath);
            continue;
        }

#if !(defined(_WIN32) || defined(WIN32))
        /* Set UID/GID on Unix-like systems */
        if (fileStat.st_uid != (uid_t)-1) {
            neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("UID"), 2, (unsigned short)fileStat.st_uid);
        }
        if (fileStat.st_gid != (gid_t)-1) {
            neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("GID"), 1, (unsigned char)fileStat.st_gid);
        }
#endif

        if (S_ISDIR(fileStat.st_mode)) {
            /* Handle directory */
            neo_aa_header_set_field_string(header, NEO_AA_FIELD_C("PAT"), strlen(relativePath), relativePath);
            neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("TYP"), 1, 'D');
            
            NeoAAArchiveItem item = neo_aa_archive_item_create_with_header(header);
            if (!item) {
                fprintf(stderr, "Failed to create item for directory: %s\n", fullPath);
                neo_aa_header_destroy_nozero(header);
                continue;
            }
            
            items[(*itemsCount)++] = item;
            
            /* Recursively process subdirectory */
            int result = add_directory_contents_to_archive(fullPath, items, itemsCount, itemsMalloc, relativePath);
            if (result != 0) {
                closedir(dir);
                return result;
            }
        } else if (S_ISLNK(fileStat.st_mode)) {
#if !(defined(_WIN32) || defined(WIN32))
            /* Handle symlink */
            char symlinkTarget[1024];
            ssize_t len = readlink(fullPath, symlinkTarget, sizeof(symlinkTarget) - 1);
            if (len < 0) {
                perror("readlink failed");
                neo_aa_header_destroy_nozero(header);
                continue;
            }
            symlinkTarget[len] = '\0';
            
            neo_aa_header_set_field_string(header, NEO_AA_FIELD_C("PAT"), strlen(relativePath), relativePath);
            neo_aa_header_set_field_string(header, NEO_AA_FIELD_C("LNK"), len, symlinkTarget);
            neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("TYP"), 1, 'L');
            
            NeoAAArchiveItem item = neo_aa_archive_item_create_with_header(header);
            if (!item) {
                fprintf(stderr, "Failed to create item for symlink: %s\n", fullPath);
                neo_aa_header_destroy_nozero(header);
                continue;
            }
            
            items[(*itemsCount)++] = item;
#endif
        } else if (S_ISREG(fileStat.st_mode)) {
            /* Handle regular file */
#ifdef O_SYMLINK
            /* On macOS but not Linux, O_SYMLINK behaves like O_NOFOLLOW,
             *  O_NOFOLLOW makes open() fail on symlinks...
             */
            int fd = open(fullPath, O_RDONLY | O_SYMLINK);
#else
            int fd = open(fullPath, O_RDONLY | O_NOFOLLOW);
#endif
            if (fd < 0) {
                perror("Failed to open file");
                neo_aa_header_destroy_nozero(header);
                continue;
            }
            
            size_t fileSize = fileStat.st_size;
            unsigned char *fileData = (unsigned char *)malloc(fileSize);
            if (!fileData) {
                fprintf(stderr, "Memory allocation failed for file: %s\n", fullPath);
                close(fd);
                neo_aa_header_destroy_nozero(header);
                continue;
            }
            
            ssize_t bytesRead = read(fd, fileData, fileSize);
            close(fd);
            
            if (bytesRead < (ssize_t)fileSize) {
                fprintf(stderr, "Failed to read entire file: %s\n", fullPath);
                free(fileData);
                neo_aa_header_destroy_nozero(header);
                continue;
            }
            
            neo_aa_header_set_field_string(header, NEO_AA_FIELD_C("PAT"), strlen(relativePath), relativePath);
            neo_aa_header_set_field_uint(header, NEO_AA_FIELD_C("TYP"), 1, 'F');
            neo_aa_header_set_field_blob(header, NEO_AA_FIELD_C("DAT"), 0, fileSize);
            
            NeoAAArchiveItem item = neo_aa_archive_item_create_with_header(header);
            if (!item) {
                fprintf(stderr, "Failed to create item for file: %s\n", fullPath);
                free(fileData);
                neo_aa_header_destroy_nozero(header);
                continue;
            }
            
            neo_aa_archive_item_add_blob_data(item, (char *)fileData, fileSize);
            items[(*itemsCount)++] = item;
            free(fileData);
        }
    }
    
    closedir(dir);
    return 0;
}

int create_aar_from_directory(const char *dirPath, const char *outputPath) {
    size_t itemsCount = 0;
    size_t itemsMalloc = 100;
    NeoAAArchiveItemList items = (NeoAAArchiveItemList)malloc(sizeof(NeoAAArchiveItem) * itemsMalloc);
    if (!items) {
        fprintf(stderr, "Failed to allocate initial items array\n");
        return -1;
    }
    
    /* Process directory recursively */
    int result = add_directory_contents_to_archive(dirPath, items, &itemsCount, &itemsMalloc, "");
    if (result != 0) {
        neo_aa_archive_item_list_destroy_nozero(items, itemsCount);
        return result;
    }
    
    /* Create archive if we found any items */
    if (itemsCount > 0) {
        NeoAAArchivePlain archive = neo_aa_archive_plain_create_with_items_nocopy(items, itemsCount);
        if (!archive) {
            fprintf(stderr, "Failed to create archive from items\n");
            neo_aa_archive_item_list_destroy_nozero(items, itemsCount);
            return -2;
        }
        
        /* Write the archive */
        neo_aa_archive_plain_write_path(archive, outputPath);
        neo_aa_archive_plain_destroy_nozero(archive);
    } else {
        free(items);
        fprintf(stderr, "No items found to archive\n");
        return -3;
    }
    
    return 0;
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
    
    /* NEOAA_CMD_VERSION is the only command where inputPath is not needed */
    if (NEOAA_CMD_VERSION == neoaaCommand) {
        printf("1.0 Beta 2\n");
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
    } else if (NEOAA_CMD_ARCHIVE == neoaaCommand) {
        if (!outputPath) {
            printf("No -o specified.\n");
            return 0;
        }
        create_aar_from_directory(inputPath, outputPath);
    }
    return 0;
}

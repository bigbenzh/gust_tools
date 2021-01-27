/*
  gust_elixir - Archive unpacker for Gust (Koei/Tecmo) .elixir[.gz] files
  Copyright © 2019-2021 VitaSmith

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#ifndef _WIN32
#include <unistd.h>
#define _unlink unlink
#endif

#include "utf8.h"
#include "util.h"
#include "parson.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_TIME
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_MALLOC
#include "miniz_tinfl.h"
#include "miniz_tdef.h"

#define EARC_MAGIC              0x45415243  // 'EARC'
#define DEFAULT_CHUNK_SIZE      0x4000

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t filename_size;
    uint32_t payload_size;
    uint32_t header_size;
    uint32_t table_size;
    uint32_t nb_files;
    uint32_t flags;             // Can be 0x0 or 0xA
} lxr_header;

typedef struct {
    uint32_t offset;
    uint32_t size;
    char     filename[0];
} lxr_entry;
#pragma pack(pop)

int main_utf8(int argc, char** argv)
{
    int r = -1;
    char path[256];
    uint8_t *buf = NULL, *zbuf = NULL;
    uint32_t zsize, lxr_entry_size = sizeof(lxr_entry);
    FILE *file = NULL, *dst = NULL;
    JSON_Value* json = NULL;
    tdefl_compressor* compressor = NULL;
    bool list_only = (argc == 3) && (argv[1][0] == '-') && (argv[1][1] == 'l');

    if ((argc != 2) && !list_only) {
        printf("%s %s (c) 2019-2021 VitaSmith\n\n"
            "Usage: %s [-l] <elixir[.gz]> file>\n\n"
            "Extracts (file) or recreates (directory) a Gust .elixir archive.\n\n"
            "Note: A backup (.bak) of the original is automatically created, when the target\n"
            "is being overwritten for the first time.\n",
            _appname(argv[0]), GUST_TOOLS_VERSION_STR, _appname(argv[0]));
        return 0;
    }

    if (is_directory(argv[argc - 1])) {
        if (list_only) {
            fprintf(stderr, "ERROR: Option -l is not supported when creating an archive\n");
            goto out;
        }
        snprintf(path, sizeof(path), "%s%celixir.json", argv[argc - 1], PATH_SEP);
        if (!is_file(path)) {
            fprintf(stderr, "ERROR: '%s' does not exist\n", path);
            goto out;
        }
        json = json_parse_file_with_comments(path);
        if (json == NULL) {
            fprintf(stderr, "ERROR: Can't parse JSON data from '%s'\n", path);
            goto out;
        }
        bool uses_older_version = (json_object_get_value(json_object(json), "version") != NULL);
        const char* filename = json_object_get_string(json_object(json), "name");
        if (filename == NULL)
            goto out;
        printf("Creating '%s'...\n", filename);
        create_backup(filename);
        // Work with a temporary file if we're going to compress it
        if (json_object_get_boolean(json_object(json), "compressed"))
            snprintf(path, sizeof(path), "%s.tmp", filename);
        else
            strncpy(path, filename, sizeof(path));
        path[sizeof(path) - 1] = 0;
        file = fopen_utf8(path, "wb+");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Can't create file '%s'\n", path);
            goto out;
        }
        lxr_header hdr = { 0 };
        hdr.magic = EARC_MAGIC;
        hdr.filename_size = json_object_get_uint32(json_object(json),
            uses_older_version ? "version" : "filename_size");
        lxr_entry_size += 0x20 + (hdr.filename_size << 4);
        hdr.nb_files = json_object_get_uint32(json_object(json), "nb_files");
        hdr.flags = json_object_get_uint32(json_object(json), "flags");
        hdr.header_size = json_object_get_uint32(json_object(json), "header_size");
        hdr.table_size = json_object_get_uint32(json_object(json), "table_size");
        if (fwrite(&hdr, sizeof(hdr), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't write header\n");
            goto out;
        }
        if (hdr.nb_files * lxr_entry_size != hdr.table_size) {
            fprintf(stderr, "ERROR: Unexpected size for offset table\n");
            goto out;
        }
        JSON_Array* json_files_array = json_object_get_array(json_object(json), "files");
        if (json_array_get_count(json_files_array) != hdr.nb_files) {
            fprintf(stderr, "ERROR: number of files doesn't match header\n");
            goto out;
        }

        lxr_entry* table = (lxr_entry*)calloc(hdr.nb_files, lxr_entry_size);
        // Allocate the space in file - we'll update it later on
        if (fwrite(table, lxr_entry_size, hdr.nb_files, file) != hdr.nb_files) {
            fprintf(stderr, "ERROR: Can't write header table\n");
            free(table);
            goto out;
        }
        printf("OFFSET   SIZE     NAME\n");
        lxr_entry* entry = table;
        for (uint32_t i = 0; i < hdr.nb_files; i++) {
            entry->offset = ftell(file);
            snprintf(path, sizeof(path), "%s%c%s", _basename(argv[argc - 1]), PATH_SEP,
                json_array_get_string(json_files_array, i));
            entry->size = read_file(path, &buf);
            if (entry->size == 0) {
                free(table);
                goto out;
            }
            strncpy(entry->filename, json_array_get_string(json_files_array, i),
                0x20 + ((size_t)hdr.filename_size << 4));
            printf("%08x %08x %s\n", entry->offset, entry->size, path);
            if (fwrite(buf, 1, entry->size, file) != entry->size) {
                fprintf(stderr, "ERROR: Can't add file data\n");
                free(table);
                goto out;
            }
            free(buf);
            buf = NULL;
            entry = (lxr_entry*) &((uint8_t*)entry)[lxr_entry_size];
        }
        hdr.payload_size = ftell(file) - hdr.header_size - hdr.table_size;
        fseek(file, 2 * sizeof(uint32_t), SEEK_SET);
        if (fwrite(&hdr.payload_size, sizeof(uint32_t), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't update file size\n");
            free(table);
            goto out;
        }
        fseek(file, hdr.header_size, SEEK_SET);
        if (fwrite(table, lxr_entry_size, hdr.nb_files, file) != hdr.nb_files) {
            fprintf(stderr, "ERROR: Can't write header table\n");
            free(table);
            goto out;
        }
        free(table);

        if (json_object_get_boolean(json_object(json), "compressed")) {
            printf("Compressing...\n");
            compressor = calloc(1, sizeof(tdefl_compressor));
            if (compressor == NULL)
                goto out;
            dst = fopen_utf8(filename, "wb");
            if (dst == NULL) {
                fprintf(stderr, "ERROR: Can't create compressed file\n");
                goto out;
            }
            fseek(file, 0, SEEK_SET);
            buf = malloc(DEFAULT_CHUNK_SIZE);
            zbuf = malloc(DEFAULT_CHUNK_SIZE);
            while (1) {
                size_t written = DEFAULT_CHUNK_SIZE;
                size_t read = fread(buf, 1, DEFAULT_CHUNK_SIZE, file);
                if (read == 0)
                    break;
                tdefl_status status = tdefl_init(compressor, NULL, NULL,
                    TDEFL_WRITE_ZLIB_HEADER | TDEFL_COMPUTE_ADLER32 | 256);
                if (status != TDEFL_STATUS_OKAY) {
                    fprintf(stderr, "ERROR: Can't init compressor\n");
                    goto out;
                }
                status = tdefl_compress(compressor, buf, &read, zbuf, &written, TDEFL_FINISH);
                if (status != TDEFL_STATUS_DONE) {
                    fprintf(stderr, "ERROR: Can't compress data\n");
                    goto out;
                }
                if (fwrite(&written, sizeof(uint32_t), 1, dst) != 1) {
                    fprintf(stderr, "ERROR: Can't write compressed stream size\n");
                    goto out;
                }
                if (fwrite(zbuf, 1, written, dst) != written) {
                    fprintf(stderr, "ERROR: Can't write compressed data\n");
                    goto out;
                }
            }
            uint32_t end_marker = 0;
            if (fwrite(&end_marker, sizeof(uint32_t), 1, dst) != 1) {
                fprintf(stderr, "ERROR: Can't write end marker\n");
                goto out;
            }
            fclose(file);
            file = NULL;
            snprintf(path, sizeof(path), "%s.tmp", filename);
            _unlink(path);
        }

        r = 0;
    } else {
        printf("%s '%s'...\n", list_only ? "Listing" : "Extracting", _basename(argv[argc - 1]));
        char* elixir_pos = strstr(argv[argc - 1], ".elixir");
        if (elixir_pos == NULL) {
            fprintf(stderr, "ERROR: File should have a '.elixir[.gz]' extension\n");
            goto out;
        }
        char* gz_pos = strstr(argv[argc - 1], ".gz");

        file = fopen_utf8(argv[argc - 1], "rb");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Can't open elixir file '%s'", argv[argc - 1]);
            goto out;
        }

        // Some elixir.gz files are actually uncompressed versions
        if (fread(&zsize, sizeof(zsize), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't read from elixir file '%s'", argv[argc - 1]);
            goto out;
        }
        if ((zsize == EARC_MAGIC) && (gz_pos != NULL))
            gz_pos = NULL;

        fseek(file, 0L, SEEK_END);
        size_t file_size = ftell(file);
        fseek(file, 0L, SEEK_SET);

        if (gz_pos != NULL) {
            file_size *= 2;
            buf = malloc(file_size);
            if (buf == NULL)
                goto out;
            size_t pos = 0;
            while (1) {
                uint32_t file_pos = (uint32_t)ftell(file);
                if (fread(&zsize, sizeof(uint32_t), 1, file) != 1) {
                    fprintf(stderr, "ERROR: Can't read compressed stream size at position %08x\n", file_pos);
                    goto out;
                }
                if (zsize == 0)
                    break;
                zbuf = malloc(zsize);
                if (zbuf == NULL)
                    goto out;
                if (fread(zbuf, 1, zsize, file) != zsize) {
                    fprintf(stderr, "ERROR: Can't read compressed stream at position %08x\n", file_pos);
                    goto out;
                }
                // Elixirs are inflated using a constant chunk size which simplifies overflow handling
                if (pos + DEFAULT_CHUNK_SIZE > file_size) {
                    file_size *= 2;
                    uint8_t* old_buf = buf;
                    buf = realloc(buf, file_size);
                    if (buf == NULL) {
                        fprintf(stderr, "ERROR: Can't increase buffer size\n");
                        buf = old_buf;
                        goto out;
                    }
                }
                size_t s = tinfl_decompress_mem_to_mem(&buf[pos], file_size - pos, zbuf, zsize,
                    TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32);
                if ((s == 0) || (s == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)) {
                    fprintf(stderr, "ERROR: Can't decompress stream at position %08x\n", file_pos);
                    goto out;
                }
                free(zbuf);
                zbuf = NULL;
                pos += s;
            } while (zsize != 0);
            file_size = pos;

//#define DECOMPRESS_ONLY
#ifdef DECOMPRESS_ONLY
            if (!list_only) {
                FILE* dst = NULL;
                *gz_pos = 0;
                dst = fopen(argv[argc - 1], "wb");
                if (dst == NULL) {
                    fprintf(stderr, "ERROR: Can't create file '%s'\n", argv[argc - 1]);
                    goto out;
                }
                if (fwrite(buf, 1, file_size, dst) != file_size) {
                    fprintf(stderr, "ERROR: Can't write file '%s'\n", argv[argc - 1]);
                    fclose(dst);
                    goto out;
                }
                printf("%08x %s\n", (uint32_t)file_size, basename(argv[argc - 1]));
                fclose(dst);
                r = 0;
                goto out;
            }
#endif
        } else {
            buf = malloc(file_size);
            if (buf == NULL)
                goto out;
            if (fread(buf, 1, file_size, file) != file_size) {
                fprintf(stderr, "ERROR: Can't read uncompressed data");
                goto out;
            }
        }

        // Now that we have an uncompressed .elixir file, extract the files
        json = json_value_init_object();
        json_object_set_string(json_object(json), "name", _basename(argv[argc - 1]));
        json_object_set_boolean(json_object(json), "compressed", (gz_pos != NULL));

        *elixir_pos = 0;
        if (!list_only && !create_path(argv[argc - 1]))
            goto out;

        lxr_header* hdr = (lxr_header*)buf;
        if (hdr->magic != EARC_MAGIC) {
            fprintf(stderr, "ERROR: Not an elixir file (bad magic)\n");
            goto out;
        }
        if (hdr->filename_size > 0x100) {
            fprintf(stderr, "ERROR: filename_size is too large (0x%08X)\n", hdr->filename_size);
            goto out;
        }
        json_object_set_number(json_object(json), "filename_size", hdr->filename_size);
        lxr_entry_size += 0x20 + (hdr->filename_size <<= 4);
        json_object_set_number(json_object(json), "flags", hdr->flags);
        // If we find files with different additional files or name sizes
        // the following may become important to have stored
        json_object_set_number(json_object(json), "header_size", hdr->header_size);
        json_object_set_number(json_object(json), "table_size", hdr->table_size);

        if (sizeof(lxr_header) + (size_t)hdr->nb_files * lxr_entry_size + hdr->payload_size != file_size) {
            fprintf(stderr, "ERROR: File size mismatch\n");
            goto out;
        }
        json_object_set_number(json_object(json), "nb_files", hdr->nb_files);

        JSON_Value* json_files_array = json_value_init_array();
        printf("OFFSET   SIZE     NAME\n");
        for (uint32_t i = 0; i < hdr->nb_files; i++) {
            lxr_entry* entry = (lxr_entry*)&buf[sizeof(lxr_header) + (size_t)i * lxr_entry_size];
            assert(entry->offset + entry->size <= (uint32_t)file_size);
            // Ignore "dummy" entries
            if ((entry->size == 0) && (strcmp(entry->filename, "dummy") == 0))
                continue;
            json_array_append_string(json_array(json_files_array), entry->filename);
            snprintf(path, sizeof(path), "%s%c%s", argv[argc - 1], PATH_SEP, entry->filename);
            printf("%08x %08x %s\n", entry->offset, entry->size, path);
            if (list_only)
                continue;
            if (!write_file(&buf[entry->offset], entry->size, path, false))
                goto out;
        }

        json_object_set_value(json_object(json), "files", json_files_array);
        snprintf(path, sizeof(path), "%s%celixir.json", argv[argc - 1], PATH_SEP);
        if (!list_only)
            json_serialize_to_file_pretty(json, path);

        r = 0;
    }

out:
    json_value_free(json);
    free(buf);
    free(zbuf);
    free(compressor);
    if (file != NULL)
        fclose(file);
    if (dst != NULL)
        fclose(dst);

    if (r != 0) {
        fflush(stdin);
        printf("\nPress any key to continue...");
        (void)getchar();
    }

    return r;
}

CALL_MAIN

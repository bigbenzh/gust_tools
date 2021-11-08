/*
  gust_gmpk - Packer/unpacker for Gust (Koei/Tecmo) .gmpk files
  Copyright © 2021 VitaSmith

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
#include <math.h>

#include "utf8.h"
#include "util.h"
#include "parson.h"
#include "dds.h"

#define JSON_VERSION            1
#define GMPK_LE_MAGIC           0x4B504D47  // 'GMPK'
#define GMPK_BE_MAGIC           0x474D504B  // 'KPMG'
#define SPD1_LE_MAGIC           0x53445031  // '1DPS'
#define SPD1_BE_MAGIC           0x31504453  // 'SPD1'
#define NID1_LE_MAGIC           0x4E494431  // '1DIN'
#define NID1_BE_MAGIC           0x3144494E  // 'NID1'
#define EXPECTED_VERSION        0x00312E31
#define ENTRY_FLAG_HAS_G1X      0x00000001
#define MIN_HEADER_SIZE         0x100
#define REPORT_URL              "https://github.com/VitaSmith/gust_tools/issues"

#pragma pack(push, 1)
// Packed Data Structure #1
typedef struct {
    char        tag[8];         // nametag
    uint32_t    magic;          // 'SPD1'
    uint32_t    size;           // total size of this structure in bytes
    uint32_t    num_records;
    uint32_t    record_size;    // Size is a number of 32-bit words
    uint32_t    num_entries;
    uint32_t    entry_size;     // Size is a number nb of 32-bit words
    uint32_t    records_offset;
    uint32_t    entries_offset;
    uint32_t    unknown_offset;
    uint32_t    entrymap_offset;
} spd1_header;

// followed by 2 * num_records * record_size (at records_offset)
// may be followed by extra data[] (if space before entry data)
// followed by num_entries * entry_size (at entries_offset)

// Name ID structure #1
typedef struct {
    char        tag[8];         // nametag
    uint32_t    magic;          // 'NID1'
    uint32_t    size;           // total size of this structure in bytes
    uint32_t    count;
    uint32_t    max_name_len;
} nid1_header;

// followed by count * (2 * uint32_t)
// followed by count * (2 * uint16_t)
// followed by string fragments

// Structure for the packed data root entry
typedef struct {
    uint32_t    entrymap_offset;
    uint32_t    namemap_offset;
    uint32_t    namemap_size;
    uint32_t    unknown1;
    uint32_t    num_files;
    uint32_t    unknown2;
    uint32_t    max_name_len;
} root_entry;

// Structure of a packed data file entry header
typedef struct {
    uint32_t    offset;
    uint32_t    size;
} file_entry;

#pragma pack(pop)

JSON_Value* read_nid(uint8_t* buf, uint32_t size)
{
    char tag[9] = { 0 };
    if (getle32(&buf[8]) == NID1_BE_MAGIC) {
        fprintf(stderr, "ERROR: Big-Endian GMPK files are not supported\n");
        return NULL;
    }
    if (getle32(&buf[8]) != NID1_LE_MAGIC) {
        fprintf(stderr, "ERROR: Bad NID magic\n");
        return NULL;
    }
    nid1_header* hdr = (nid1_header*)buf;
    if (size < sizeof(nid1_header) || hdr->size != size) {
        fprintf(stderr, "ERROR: NID size mismatch\n");
        return NULL;
    }

    JSON_Value* json_nid = json_value_init_object();

    memcpy(tag, hdr->tag, 8);
    json_object_set_string(json_object(json_nid), "tag", (const char*)tag);
    json_object_set_string(json_object(json_nid), "type", "NID1");
    json_object_set_number(json_object(json_nid), "size", hdr->size);

    // Process name entries
    size_t offset = sizeof(nid1_header);
    uint32_t* data = (uint32_t*)&buf[offset];
    // Move our offset to the string fragments
    offset += (size_t)hdr->count * 2 * sizeof(uint32_t);
    JSON_Value* json_names_array = json_value_init_array();
    char* str = calloc((size_t)hdr->max_name_len + 1, 1);
    if (str == NULL)
        return NULL;
    for (uint32_t i = 0; i < hdr->count; i++) {
        JSON_Value* json_name = json_value_init_object();
        json_object_set_number(json_object(json_name), "index", data[2 * i]);
        json_object_set_number(json_object(json_name), "flags", data[2 * i + 1]);
        uint32_t k = 0, val = getle32(&data[2 * hdr->count + i]);
        uint8_t len = buf[offset + (val >> 16)];
        for (size_t j = 1; j <= len; j++)
            str[k++] = buf[offset + (val >> 16) + j];
        json_object_set_number(json_object(json_name), "split", k);
        len = buf[offset + (val & 0xffff)];
        for (size_t j = 1; j <= len; j++)
            str[k++] = buf[offset + (val & 0xffff) + j];
        str[k] = 0;
        json_object_set_string(json_object(json_name), "name", str);
        json_array_append_value(json_array(json_names_array), json_name);
    }
    free(str);
    json_object_set_value(json_object(json_nid), "names", json_names_array);

    return json_nid;
}

JSON_Value* read_sdp(uint8_t* buf, uint32_t size)
{
    char* tag[9] = { 0 };
    if (getle32(&buf[8]) == SPD1_BE_MAGIC) {
        fprintf(stderr, "ERROR: Big-Endian GMPK files are not supported\n");
        return NULL;
    }
    if (getle32(&buf[8]) != SPD1_LE_MAGIC) {
        fprintf(stderr, "ERROR: Bad SPD1 magic\n");
        return NULL;
    }
    spd1_header* hdr = (spd1_header*)buf;
    if (size < sizeof(spd1_header) || hdr->size > size) {
        fprintf(stderr, "ERROR: File size mismatch\n");
        return NULL;
    }

    JSON_Value* json_sdp = json_value_init_object();

    memcpy(tag, hdr->tag, 8);
    json_object_set_string(json_object(json_sdp), "tag", (const char*)tag);
    json_object_set_string(json_object(json_sdp), "type", "SPD1");
    json_object_set_number(json_object(json_sdp), "size", hdr->size);

    // Process records
    uint32_t offset = hdr->records_offset;
    JSON_Value* json_record_array = json_value_init_array();
    for (uint32_t k = 0; k < 2; k++) {
        for (uint32_t i = 0; i < hdr->num_records; i++) {
            JSON_Value* json_entry_data_array = json_value_init_array();
            for (uint32_t j = 0; j < hdr->record_size; j++)
                json_array_append_number(json_array(json_entry_data_array),
                    getle32(&buf[offset + (i * hdr->record_size + j) * sizeof_32(uint32_t)]));
            json_array_append_value(json_array(json_record_array), json_entry_data_array);
        }
        offset += (hdr->num_records * hdr->record_size) * sizeof_32(uint32_t);
    }
    json_object_set_value(json_object(json_sdp), "records", json_record_array);

    // Process extra data
    uint32_t extra_data_size = (hdr->entrymap_offset == 0) ? hdr->entries_offset : min(hdr->entries_offset, hdr->entrymap_offset);
    extra_data_size -= offset;
    if (extra_data_size > 0) {
        if (extra_data_size % sizeof_32(uint32_t)) {
            fprintf(stderr, "ERROR: Extra data size (%d) is not a multiple of %d\n",
                extra_data_size, sizeof_32(uint32_t));
            json_value_free(json_sdp);
            return NULL;
        }
        JSON_Value* json_extra_data_array = json_value_init_array();
        for (uint32_t i = 0; i < extra_data_size; i += sizeof_32(uint32_t)) {
            json_array_append_number(json_array(json_extra_data_array), getle32(&buf[offset + i]));
        }
        json_object_set_value(json_object(json_sdp), "extra_data", json_extra_data_array);
    }

    // Process entries
    offset = hdr->entries_offset;
    JSON_Value* json_entries_array = json_value_init_array();
    for (uint32_t i = 0; i < hdr->num_entries; i++) {
        JSON_Value* json_entry_data_array = json_value_init_array();
        for (uint32_t j = 0; j < hdr->entry_size; j++)
            json_array_append_number(json_array(json_entry_data_array),
                getle32(&buf[offset + (i * hdr->entry_size + j) * sizeof_32(uint32_t)]));
        json_array_append_value(json_array(json_entries_array), json_entry_data_array);
    }
    json_object_set_value(json_object(json_sdp), "entries", json_entries_array);

    // Process optional entrymap
    if (hdr->entrymap_offset != 0) {
        JSON_Value* json_entrymap = read_sdp(&buf[hdr->entrymap_offset], hdr->size - hdr->entrymap_offset);
        if (json_entrymap != NULL) {
            const char* em_tag = json_object_get_string(json_object(json_entrymap), "tag");
            if (strcmp(em_tag, "EntryMap") != 0) {
                fprintf(stderr, "ERROR: Unexpected EntryMap tag '%s'\n", em_tag);
                json_value_free(json_entrymap);
                json_value_free(json_sdp);
                return NULL;
            }
            json_object_set_value(json_object(json_sdp), "entrymap", json_entrymap);
        }

        // Validate the EntryMap and look for the NameMap
        if (hdr->num_entries != 1 || hdr->entry_size < sizeof_32(root_entry) / sizeof_32(uint32_t)) {
            fprintf(stderr, "ERROR: Unexpected entry data for the root SDP\n");
            json_value_free(json_sdp);
            return NULL;
        }

        root_entry* gmpk_root = (root_entry*)&buf[hdr->entries_offset];
        if (gmpk_root->entrymap_offset != hdr->entrymap_offset) {
            fprintf(stderr, "ERROR: EntryMap position mismatch\n");
            json_value_free(json_sdp);
            return NULL;
        }

        // Process the NameMap
        if (hdr->size - gmpk_root->namemap_offset < gmpk_root->namemap_size) {
            fprintf(stderr, "ERROR: NameMap size is too small\n");
            json_value_free(json_sdp);
            return NULL;
        }

        JSON_Value* json_nid = read_nid(&buf[gmpk_root->namemap_offset], gmpk_root->namemap_size);
        if (json_nid == NULL) {
            json_value_free(json_sdp);
            return NULL;
        }

        json_object_set_value(json_object(json_sdp), "namemap", json_nid);
    }

    return json_sdp;
}

uint16_t get_fragment(uint8_t* fragments, uint16_t *fragments_size, char* str, uint16_t str_len)
{
    uint16_t pos = 0;
    while (pos < *fragments_size) {
        bool match = (str_len == (uint16_t)fragments[pos]);
        for (uint16_t i = 0; match && i < str_len; i++)
            match = (fragments[pos + i + 1] == str[i]);
        if (match)
            return pos;
        pos += (uint16_t)fragments[pos] + 1;
    }
    // Fragment was not found => insert it
    pos = *fragments_size;
    fragments[(*fragments_size)++] = (uint8_t)str_len;
    memcpy(&fragments[*fragments_size], str, str_len);
    *fragments_size += str_len;
    return pos;
}

// TODO: validate that namemap and entrymap count are the same
uint32_t write_nid(JSON_Object* json_nid, uint8_t* buf, uint32_t size)
{
    uint32_t *data, written = 0;
    nid1_header* hdr = (nid1_header*)buf;

    if (json_nid == NULL || buf == NULL || align_to_16(size) != size ||
        size < sizeof_32(nid1_header)) {
        fprintf(stderr, "ERROR: Invalid write_nid() parameters\n");
        return 0;
    }

    // Write the header
    written = sizeof_32(nid1_header);
    const char* tag = json_object_get_string(json_nid, "tag");
    const char* type = json_object_get_string(json_nid, "type");
    if (tag == NULL || type == NULL || strcmp(type, "NID1") != 0) {
        fprintf(stderr, "ERROR: Malformed or missing NameMap data\n");
        return 0;
    }
    memcpy(hdr->tag, tag, strlen(tag));
    hdr->magic = NID1_LE_MAGIC;
    hdr->size = json_object_get_uint32(json_nid, "size");
    if (hdr->size < sizeof_32(nid1_header) || hdr->size > size) {
        fprintf(stderr, "ERROR: Improper NameMap size\n");
        return 0;
    }

    JSON_Array* json_names_array = json_object_get_array(json_nid, "names");

    hdr->count = (uint32_t)json_array_get_count(json_names_array);
    hdr->max_name_len = 0;

    if (hdr->size < sizeof_32(nid1_header) + 3 * hdr->count * sizeof_32(uint32_t)) {
        fprintf(stderr, "ERROR: NameMap buffer is to small to insert data\n");
        return 0;
    }

    // Write the name entries
    data = (uint32_t*)&buf[written];
    uint8_t* fragments = (uint8_t*)&buf[written + 3 * hdr->count * sizeof_32(uint32_t)];
    uint16_t pos, fragments_size = 0;
    uint32_t split, len;
    for (uint32_t i = 0; i < hdr->count; i++) {
        JSON_Object* json_name = json_array_get_object(json_names_array, i);
        data[2 * i] = json_object_get_uint32(json_name, "index");
        data[2 * i + 1] = json_object_get_uint32(json_name, "flags");
        split = json_object_get_uint32(json_name, "split");
        char c, *name = strdup(json_object_get_string(json_name, "name"));
        len = (uint32_t)strlen(name);
        assert(name && split <= len);
        hdr->max_name_len = max(hdr->max_name_len, len);
        c = name[split];
        name[split] = 0;
        pos = get_fragment(fragments, &fragments_size, name, (uint16_t)split);
        data[2 * hdr->count + i] = pos + hdr->count * sizeof_32(uint32_t);
        name[split] = c;
        pos = get_fragment(fragments, &fragments_size, &name[split], (uint16_t)(len - split));
        data[2 * hdr->count + i] <<= 16;
        data[2 * hdr->count + i] |= pos + hdr->count * sizeof_32(uint32_t);
        free(name);
    }
    written += 3 * hdr->count * sizeof_32(uint32_t) + fragments_size;
    written = align_to_4(written);
    if (written != hdr->size) {
        fprintf(stderr, "ERROR: Reconstructed NameMap does not match original size.\n");
        fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
        return 0;
    }
    return written;
}

uint32_t write_sdp(JSON_Object* json_sdp, uint8_t* buf, uint32_t size)
{
    spd1_header* hdr = (spd1_header*)buf;
    uint32_t* data, written = 0;

    if (json_sdp == NULL || buf == NULL || align_to_16(size) != size ||
        size < sizeof_32(spd1_header)) {
        fprintf(stderr, "ERROR: Invalid write_spd() parameters\n");
        return 0;
    }

    // Write the header
    written = sizeof_32(spd1_header);
    const char* tag = json_object_get_string(json_sdp, "tag");
    const char* type = json_object_get_string(json_sdp, "type");
    if (tag == NULL || type == NULL || strcmp(type, "SPD1") != 0) {
        fprintf(stderr, "ERROR: Malformed or missing SDP1 data\n");
        return 0;
    }
    memcpy(hdr->tag, tag, strlen(tag));
    hdr->magic = SPD1_LE_MAGIC;
    hdr->size = json_object_get_uint32(json_sdp, "size");

    // Write the records
    hdr->records_offset = written;
    data = (uint32_t*)&buf[written];
    JSON_Array* json_records_array = json_object_get_array(json_sdp, "records");
    if (json_records_array == NULL || json_array_get_count(json_records_array) % 2) {
        fprintf(stderr, "ERROR: Malformed or missing SPD1 records data\n");
        return 0;
    }
    hdr->num_records = (uint32_t)json_array_get_count(json_records_array) / 2;
    hdr->record_size = (uint32_t)json_array_get_count(json_array_get_array(json_records_array, 0));
    written += 2 * hdr->num_records * hdr->record_size * sizeof_32(uint32_t);
    written = align_to_16(written);
    if (written > size) {
        fprintf(stderr, "ERROR: SDP1 buffer is to small to insert records\n");
        return 0;
    }
    for (uint32_t i = 0; i < hdr->num_records * 2; i++) {
        JSON_Array* json_record_data = json_array_get_array(json_records_array, i);
        for (uint32_t j = 0; j < hdr->record_size; j++) {
            data[i * hdr->record_size + j] = json_array_get_uint32(json_record_data, j);
        }
    }

    // Write the (optional) extra data
    data = (uint32_t*)&buf[written];
    JSON_Array* json_extra_data = json_object_get_array(json_sdp, "extra_data");
    if (json_extra_data != NULL) {
        written += (uint32_t)json_array_get_count(json_extra_data) * sizeof_32(uint32_t);
        written = align_to_16(written);
        if (written > size) {
            fprintf(stderr, "ERROR: SDP1 buffer is to small to insert extra data\n");
            return 0;
        }
        for (uint32_t i = 0; i < (uint32_t)json_array_get_count(json_extra_data); i++)
            data[i] = json_array_get_uint32(json_extra_data, i);
    }

    // Write the entries
    hdr->entries_offset = written;
    data = (uint32_t*)&buf[written];
    JSON_Array* json_entries_array = json_object_get_array(json_sdp, "entries");
    if (json_entries_array == NULL) {
        fprintf(stderr, "ERROR: JSON SPD1 data is missing an entries array\n");
        return 0;
    }
    hdr->num_entries = (uint32_t)json_array_get_count(json_entries_array);
    hdr->entry_size = (uint32_t)json_array_get_count(json_array_get_array(json_entries_array, 0));
    written += hdr->num_entries * hdr->entry_size * sizeof_32(uint32_t);
    written = align_to_16(written);
    if (written > size) {
        fprintf(stderr, "ERROR: SDP1 buffer is to small to insert entries\n");
        return 0;
    }
    for (uint32_t i = 0; i < hdr->num_entries; i++) {
        JSON_Array* json_entry_data = json_array_get_array(json_entries_array, i);
        for (uint32_t j = 0; j < hdr->entry_size; j++) {
            data[i * hdr->entry_size + j] = json_array_get_uint32(json_entry_data, j);
        }
    }

    // Write the (optional) EntryMap and NameMap
    JSON_Object* json_entrymap = json_object_get_object(json_sdp, "entrymap");
    if (json_entrymap != NULL) {
        uint32_t w = 0;
        hdr->entrymap_offset = written;
        if ((w = write_sdp(json_entrymap, &buf[written], size - written)) == 0)
            return 0;
        written = align_to_16(written + w);

        // An EntryMap should be followed by a NameMap
        // The previous write_sdp() call made sure that the offset is aligned
        JSON_Object* json_namemap = json_object_get_object(json_sdp, "namemap");
        if ((w = write_nid(json_namemap, &buf[written], size - written)) == 0)
            return 0;
        written = align_to_16(written + w);

        // Validate the root entry data
        root_entry* gmpk_root = (root_entry*)&buf[hdr->entries_offset];
        if (getle32(&buf[gmpk_root->entrymap_offset + 8]) != SPD1_LE_MAGIC) {
            fprintf(stderr, "ERROR: EntryMap offset mismatch\n");
            return 0;
        }
        if (getle32(&buf[gmpk_root->namemap_offset + 8]) != NID1_LE_MAGIC) {
            fprintf(stderr, "ERROR: NameMap offset mismatch\n");
            return 0;
        }
        if (getle32(&buf[gmpk_root->namemap_offset + 12]) != gmpk_root->namemap_size) {
            fprintf(stderr, "ERROR: NameMap size mismatch\n");
            return 0;
        }
        if (getle32(&buf[gmpk_root->namemap_offset + 20]) != gmpk_root->max_name_len) {
            fprintf(stderr, "ERROR: NameMap max name length mismatch\n");
            return 0;
        }
    }

    if (written != hdr->size) {
        fprintf(stderr, "ERROR: Reconstructed SPD1 does not match original size.\n");
        fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
        return 0;
    }

    // TODO: check that last offset = json SDP size
    return written;
}

int main_utf8(int argc, char** argv)
{
    static const char* extension[2] = { ".g1m", ".g1t" };
    char path[256], *dir = NULL;
    int r = -1;
    JSON_Value* json = NULL;
    FILE *file = NULL;
    uint8_t* buf = NULL;
    uint32_t* offset_table = NULL;
    bool list_only = (argc == 3) && (argv[1][0] == '-') && (argv[1][1] == 'l');

    if ((argc != 2) && !list_only) {
        printf("%s %s (c) 2021 VitaSmith\n\n"
            "Usage: %s [-l] <file or directory>\n\n"
            "Extracts (file) or recreates (directory) a Gust .gmpk model pack.\n\n"
            "Note: A backup (.bak) of the original is automatically created, when the target\n"
            "is being overwritten for the first time.\n",
            _appname(argv[0]), GUST_TOOLS_VERSION_STR, _appname(argv[0]));
        return 0;
    }

    if (!is_directory(argv[argc - 1])) {
        printf("%s '%s'...\n", list_only ? "Listing" : "Extracting", argv[argc - 1]);
        size_t len = strlen(argv[argc - 1]);
        if ((len < 5) || (argv[argc - 1][len - 5] != '.') || (argv[argc - 1][len - 4] != 'g') ||
            (argv[argc - 1][len - 3] != 'm') || (argv[argc - 1][len - 2] != 'p') ||
            (argv[argc - 1][len - 1] != 'k')) {
            fprintf(stderr, "ERROR: File should have a '.gmpk' extension\n");
            goto out;
        }
        char* gmpk_pos = &argv[argc - 1][len - 5];

        uint32_t file_size = read_file(argv[argc - 1], &buf);
        if (file_size == UINT32_MAX)
            goto out;

        if (getle32(buf) == GMPK_BE_MAGIC) {
            fprintf(stderr, "ERROR: Big-Endian GMPK files are not supported\n");
            goto out;
        }
        if (getle32(buf) != GMPK_LE_MAGIC) {
            fprintf(stderr, "ERROR: Not a GMPK file (bad magic) or unsupported platform\n");
            goto out;
        }

        if (getle32(&buf[4]) != EXPECTED_VERSION) {
            fprintf(stderr, "ERROR: Unsupported GMPK version\n");
            goto out;
        }

        // Keep the information required to recreate the package in a JSON file
        json = json_value_init_object();
        json_object_set_number(json_object(json), "json_version", JSON_VERSION);
        json_object_set_string(json_object(json), "name", _basename(argv[argc - 1]));

        gmpk_pos[0] = 0;
        if (!list_only && !create_path(argv[argc - 1]))
            goto out;

        dir = strdup(argv[argc - 1]);
        if (dir == NULL) {
            fprintf(stderr, "ERROR: Alloc error\n");
            goto out;
        }
        dir[get_trailing_slash(dir)] = 0;

        JSON_Value* json_gmpk = read_sdp(buf, file_size);
        if (json_gmpk == NULL)
            goto out;

        json_object_set_value(json_object(json), "gmpk", json_gmpk);

        uint32_t offset = ((uint32_t*)buf)[3];
        file_entry* fe = (file_entry*)&buf[offset];

        JSON_Array* json_entries = json_object_dotget_array(json_object(json_gmpk), "entrymap.entries");
        JSON_Array* json_names = json_object_dotget_array(json_object(json_gmpk), "namemap.names");
        if (json_entries == NULL || json_array_get_count(json_entries) == 0 ||
            json_names == NULL || json_array_get_count(json_names) == 0 ||
            json_array_get_count(json_entries) != json_array_get_count(json_names)) {
            fprintf(stderr, "ERROR: Failed to process EntryMap/NameMap\n");
            goto out;
        }

        printf("OFFSET   SIZE     NAME\n");
        for (size_t i = 0; i < json_array_get_count(json_entries); i++) {
            const char* name = json_object_get_string(json_array_get_object(json_names, i), "name");
            JSON_Array* json_entry_data = json_array_get_array(json_entries, i);
            assert(json_array_get_count(json_entry_data) >= 4);
            for (size_t j = 0; j < 2; j++) {
                if (json_array_get_uint32(json_entry_data, 2 * j) & ENTRY_FLAG_HAS_G1X) {
                    uint32_t index = json_array_get_uint32(json_entry_data, 2 * j + 1);
                    snprintf(path, sizeof(path), "%s%s%c%s%s", dir,
                       _basename(argv[argc - 1]), PATH_SEP, name, extension[j]);
                    // Sanity checks
                    if (offset + fe[index].size > file_size || offset + fe[index].offset > file_size) {
                        fprintf(stderr, "ERROR: Invalid file size or file offset\n");
                        goto out;
                    }
                    printf("%08x %08x %s%s\n", offset + fe[index].offset,
                        fe[index].size, name, extension[j]);
                    if (list_only)
                        continue;
                    FILE* dst = fopen_utf8(path, "wb");
                    if (dst == NULL) {
                        fprintf(stderr, "ERROR: Can't create file '%s'\n", path);
                        goto out;
                    }
                    if (fwrite(&buf[offset + fe[index].offset], 1, fe[index].size, dst) != fe[index].size) {
                        fprintf(stderr, "ERROR: Can't write file '%s'\n", path);
                        fclose(dst);
                        goto out;
                    }
                    fclose(dst);
                }
            }
        }

        if (!list_only) {
            snprintf(path, sizeof(path), "%s%cgmpk.json", argv[argc - 1], PATH_SEP);
            json_serialize_to_file_pretty(json, path);
        }

        r = 0;
    } else {
        if (list_only) {
            fprintf(stderr, "ERROR: Option -l is not supported when creating an archive\n");
            goto out;
        }
        snprintf(path, sizeof(path), "%s%cgmpk.json", argv[argc - 1], PATH_SEP);
        if (!is_file(path)) {
            fprintf(stderr, "ERROR: '%s' does not exist\n", path);
            goto out;
        }
        json = json_parse_file_with_comments(path);
        if (json == NULL) {
            fprintf(stderr, "ERROR: Can't parse JSON data from '%s'\n", path);
            goto out;
        }
        const uint32_t json_version = json_object_get_uint32(json_object(json), "json_version");
        if (json_version != JSON_VERSION) {
            fprintf(stderr, "ERROR: This utility is not compatible with the JSON file provided.\n"
                "You need to (re)extract the '.gmpk' using this application.\n");
            goto out;
        }
        const char* filename = json_object_get_string(json_object(json), "name");
        if (filename == NULL)
            goto out;
        strcpy(path, argv[argc - 1]);
        if (get_trailing_slash(path) != 0)
            path[get_trailing_slash(path)] = 0;
        else
            path[0] = 0;
        strcat(path, filename);
        path[sizeof(path) - 1] = 0;
        printf("Creating '%s'...\n", path);
        create_backup(path);
        file = fopen_utf8(path, "wb+");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Can't create file '%s'\n", path);
            goto out;
        }

        JSON_Object* json_gmpk = json_object_get_object(json_object(json), "gmpk");
        if (json_gmpk == NULL) {
            fprintf(stderr, "ERROR: Missing JSON GMPK element\n");
            goto out;
        }

        uint32_t header_size = json_object_get_uint32(json_gmpk, "size");
        if (header_size < MIN_HEADER_SIZE) {
            fprintf(stderr, "ERROR: GMPK header size is too small (%d)\n", header_size);
            goto out;
        }

        JSON_Array* json_entries = json_object_dotget_array(json_gmpk, "entrymap.entries");
        JSON_Array* json_names = json_object_dotget_array(json_gmpk, "namemap.names");
        if (json_entries == NULL || json_array_get_count(json_entries) == 0 ||
            json_names == NULL || json_array_get_count(json_names) == 0 ||
            json_array_get_count(json_entries) != json_array_get_count(json_names)) {
            fprintf(stderr, "ERROR: Invalid EntryMap/NameMap JSON entries\n");
            goto out;
        }

        uint32_t num_files = json_array_get_uint32(json_array_get_array(json_object_get_array(json_gmpk, "entries"), 0), 4);
        uint32_t fe_size = align_to_16((num_files + 1) * (uint32_t)sizeof(file_entry));
        // Add space for the file entry header
        buf = calloc((size_t)header_size + fe_size, 1);
        if (buf == NULL)
            goto out;

        if (write_sdp(json_gmpk, buf, header_size) == 0)
            goto out;

        file_entry* fe = (file_entry*)&buf[header_size];

        // We will fill the file offsets/size later
        fwrite(buf, 1, (size_t)header_size + fe_size, file);

        printf("OFFSET   SIZE     NAME\n");
        dir = strdup(argv[argc - 1]);
        if (dir == NULL) {
            fprintf(stderr, "ERROR: Alloc error\n");
            goto out;
        }
        dir[get_trailing_slash(dir)] = 0;
        uint8_t padding_buf[0x10] = { 0 };
        for (size_t i = 0; i < json_array_get_count(json_entries); i++) {
            const char* name = json_object_get_string(json_array_get_object(json_names, i), "name");
            JSON_Array* json_entry_data = json_array_get_array(json_entries, i);
            assert(json_array_get_count(json_entry_data) >= 4);
            for (size_t j = 0; j < 2; j++) {
                if (json_array_get_uint32(json_entry_data, 2 * j) & ENTRY_FLAG_HAS_G1X) {
                    uint32_t index = json_array_get_uint32(json_entry_data, 2 * j + 1);
                    snprintf(path, sizeof(path), "%s%s%c%s%s", dir,
                        _basename(argv[argc - 1]), PATH_SEP, name, extension[j]);
                    uint8_t* sb = NULL;
                    fe[index].offset = ftell(file) - header_size;
                    assert(fe[index].offset % 0x10 == 0);
                    fe[index].size = read_file(path, &sb);
                    if (fe[index].size == UINT32_MAX)
                        goto out;
                    printf("%08x %08x %s%s\n", (uint32_t)ftell(file), fe[index].size, name, extension[j]);
                    if (fwrite(sb, 1, fe[index].size, file) != fe[index].size) {
                        fprintf(stderr, "ERROR: Can't add data from '%s'\n", path);
                        free(sb);
                        goto out;
                    }
                    free(sb);
                    // Pad to 16 bytes
                    if (fe[index].size % 0x10 != 0)
                        fwrite(padding_buf, 1, 0x10 - (fe[index].size % 0x10), file);
                }
            }
        }
        fe[num_files].offset = ftell(file);
        assert(fe[num_files].offset % 0x10 == 0);
        fseek(file, header_size, SEEK_SET);
        if (fwrite(fe, 1, fe_size, file) != fe_size) {
            fprintf(stderr, "ERROR: Can't write file entry data\n");
            goto out;
        }
        r = 0;
    }

out:
    json_value_free(json);
    free(buf);
    free(dir);
    free(offset_table);
    if (file != NULL)
        fclose(file);

    if (r != 0) {
        fflush(stdin);
        printf("\nPress any key to continue...");
        (void)getchar();
    }

    return r;
}

CALL_MAIN

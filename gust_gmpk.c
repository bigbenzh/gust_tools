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

#include "utf8.h"
#include "util.h"
#include "parson.h"

#define JSON_VERSION            2
#define GMPK_MAGIC              0x4B504D47  // 'GMPK'
#define SDP1_LE_MAGIC           0x53445031  // '1PDS'
#define SDP1_BE_MAGIC           0x31504453  // 'SDP1'
#define NID1_LE_MAGIC           0x4E494431  // '1DIN'
#define NID1_BE_MAGIC           0x3144494E  // 'NID1'
#define EXPECTED_VERSION        0x00312E31
#define MIN_HEADER_SIZE         0x100
#define MAX_HEADER_SIZE         0x10000
#define MAX_NAMES_COUNT         0x100
#define REPORT_URL              "https://github.com/VitaSmith/gust_tools/issues"

static const char* known_sdp_tags[] = {
    "GMPK1.1",
    "EntryMap",
};

static const char* known_nid_tags[] = {
    "NameMap",
};

static const char* extension[] = {
    ".g1m",
    ".g1t",
    ".g1h"
};

#pragma pack(push, 1)
// Structure Packed Data, Type 1
typedef struct {
    char        tag[8];         // nametag
    uint32_t    magic;          // 'SDP1'
    uint32_t    size;           // total size of this structure in bytes
    uint32_t    data_count;     // total number of data records / 2
    uint32_t    data_record_size; // Size of a data record, in 32-bit words
    uint32_t    entry_count;    // total number of entries
    uint32_t    entry_record_size; // Size of an entry record, in 32-bit words
                                // For an EntryMap this seems to be data_count * 2
    uint32_t    data_offset;
    uint32_t    entry_offset;
    uint32_t    unknown_offset;
    uint32_t    entrymap_offset;
} sdp1_header;

// followed by 2 * data_count [+1] * data_record_size (at data_offset)
// followed by entry_count * entry_record_size (at entry_offset)

// Name ID structure, Type 1
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
    uint32_t    files_count;
    uint32_t    unknown2;
    uint32_t    max_name_len;
} root_entry;

// Structure of a model entry
typedef struct {
    uint32_t    has_component;
    uint32_t    file_index;
} model_component;

typedef struct {
    model_component component[array_size(extension) + 1];
} model_entry;

// Structure of a packed data file entry header
typedef struct {
    uint32_t    offset;
    uint32_t    size;
} file_entry;

#pragma pack(pop)

static uint32_t *entry_data = NULL, entry_data_size, entry_data_count, files_count;

JSON_Value* read_nid(uint8_t* buf, uint32_t size)
{
    char tag[9] = { 0 };
    nid1_header* hdr = (nid1_header*)buf;
    if (sizeof(nid1_header) > size) {
        fprintf(stderr, "ERROR: NID buffer is too small\n");
        return NULL;
    }
    if (data_endianness != platform_endianness) {
        BSWAP_UINT32(hdr->magic);
        BSWAP_UINT32(hdr->size);
        BSWAP_UINT32(hdr->count);
        BSWAP_UINT32(hdr->max_name_len);
    }
    // Endianness should have been detected when processing the SDP
    if (hdr->magic == NID1_BE_MAGIC) {
        fprintf(stderr, "ERROR: NID endianness mismatch\n");
        return NULL;
    }
    if (hdr->magic != NID1_LE_MAGIC) {
        fprintf(stderr, "ERROR: Bad NID magic\n");
        return NULL;
    }
    if (hdr->size != size) {
        fprintf(stderr, "ERROR: NID size mismatch\n");
        return NULL;
    }

    memcpy(tag, hdr->tag, 8);
    for (uint32_t i = 0; i < array_size(known_nid_tags); i++) {
        if (strcmp(tag, known_nid_tags[i]) == 0)
            break;
        if (i == array_size(known_nid_tags) - 1) {
            fprintf(stderr, "ERROR: Unsupported SDP tag '%s'.\n", tag);
            fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
            return NULL;
        }
    }

    JSON_Value* json_nid = json_value_init_object();
    json_object_set_string(json_object(json_nid), "tag", (const char*)tag);
    json_object_set_string(json_object(json_nid), "type", "NID1");

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
        uint32_t k = 0, val = getp32(&data[2 * hdr->count + i]);
        uint8_t len = buf[offset + (val >> 16)];
        if (len > hdr->max_name_len) {
            fprintf(stderr, "ERROR: Fragment length (%d) is greater than %d.\n", len, hdr->max_name_len);
            return NULL;
        }
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
    char tag[9] = { 0 };
    sdp1_header* hdr = (sdp1_header*)buf;
    if (sizeof(sdp1_header) > size) {
        fprintf(stderr, "ERROR: SDP buffer is too small\n");
        return NULL;
    }
    if (hdr->magic != SDP1_LE_MAGIC && hdr->magic != SDP1_BE_MAGIC) {
        fprintf(stderr, "ERROR: Bad SDP magic\n");
        return NULL;
    }
    if (getle32(&buf[8]) == SDP1_BE_MAGIC)
        data_endianness = big_endian;
    if (data_endianness != platform_endianness) {
        BSWAP_UINT32(hdr->magic);
        BSWAP_UINT32(hdr->size);
        BSWAP_UINT32(hdr->data_count);
        BSWAP_UINT32(hdr->data_record_size);
        BSWAP_UINT32(hdr->entry_count);
        BSWAP_UINT32(hdr->entry_record_size);
        BSWAP_UINT32(hdr->data_offset);
        BSWAP_UINT32(hdr->entry_offset);
        BSWAP_UINT32(hdr->unknown_offset);
        BSWAP_UINT32(hdr->entrymap_offset);
    }
    if (hdr->size > size) {
        fprintf(stderr, "ERROR: SDP size mismatch\n");
        return NULL;
    }
    if (hdr->size > MAX_HEADER_SIZE) {
        fprintf(stderr, "ERROR: SPD header is larger than %d KB.\n", MAX_HEADER_SIZE / 1024);
        fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
        return NULL;
    }

    memcpy(tag, hdr->tag, 8);
    for (uint32_t i = 0; i < array_size(known_sdp_tags); i++) {
        if (strcmp(tag, known_sdp_tags[i]) == 0)
            break;
        if (i == array_size(known_sdp_tags) - 1) {
            fprintf(stderr, "ERROR: Unsupported SDP tag '%s'.\n", tag);
            fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
            return NULL;
        }
    }

    JSON_Value* json_sdp = json_value_init_object();
    json_object_set_string(json_object(json_sdp), "tag", (const char*)tag);
    json_object_set_string(json_object(json_sdp), "type", "SDP1");

    // Process data
    uint32_t offset = hdr->data_offset;
    uint32_t data_size = hdr->entry_offset - hdr->data_offset;
    uint32_t data_count = data_size / (2 * hdr->data_record_size * sizeof_32(uint32_t));
    if (data_size % (hdr->data_record_size * sizeof_32(uint32_t))) {
        fprintf(stderr, "ERROR: Computed data size is not a multiple of the record size.\n");
        fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
        return NULL;
    }
    if (data_count != hdr->data_count) {
        fprintf(stderr, "ERROR: Computed data_count (%d) does not match actual value (%d).\n",
            data_count, hdr->data_count);
        fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
        return NULL;
    }
    JSON_Value* json_data_array = json_value_init_array();
    JSON_Value* json_data_record_array = NULL;
    for (uint32_t i = 0; i < data_size / hdr->data_record_size; ) {
        if (json_data_record_array == NULL)
            json_data_record_array = json_value_init_array();
        json_array_append_number(json_array(json_data_record_array),
            getp32(&buf[offset + i * sizeof_32(uint32_t)]));
        i++;
        if (i % hdr->data_record_size == 0) {
            json_array_append_value(json_array(json_data_array), json_data_record_array);
            json_data_record_array = NULL;
        }
    }
    json_object_set_value(json_object(json_sdp), "data", json_data_array);

    // If we are an EntryMap, validate that it matches our expectations
    if (strcmp(tag, known_sdp_tags[1]) == 0) {
        if (hdr->entry_record_size != 2 * hdr->data_count) {
            fprintf(stderr, "ERROR: Unexpected EntryMap record size\n");
            fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
            json_value_free(json_sdp);
            return NULL;
        }
        model_entry* me = (model_entry*)&buf[hdr->entry_offset];
        if (getle32(&me->component[0].has_component) == 1) {
            // Only check submodels if we actually have a model
            if (hdr->entry_count > 1 && (getle32(&(me->component[hdr->entry_record_size / 2 - 1].has_component)) != 1 ||
                getle32(&(me->component[hdr->entry_record_size / 2 - 1].file_index)) != hdr->entry_count - 1)) {
                fprintf(stderr, "ERROR: Unexpected EntryMap submodel count\n");
                fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
                json_value_free(json_sdp);
                return NULL;
            }
            for (uint32_t i = 1; i < hdr->entry_count; i++) {
                me = (model_entry*)&buf[hdr->entry_offset + (i * hdr->entry_record_size * sizeof_32(uint32_t))];
                if (getle32(&me->component[hdr->entry_record_size / 2 - 1].has_component) != 1 ||
                    me->component[hdr->entry_record_size / 2 - 1].file_index != 0xffffffff) {
                    fprintf(stderr, "ERROR: More than one level of EntryMap submodels\n");
                    fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
                    json_value_free(json_sdp);
                    return NULL;
                }
            }
        }
    }

    // Process optional EntryMap SDP
    if (hdr->entrymap_offset != 0) {
        JSON_Value* json_entrymap = read_sdp(&buf[hdr->entrymap_offset], hdr->size - hdr->entrymap_offset);
        if (json_entrymap == NULL) {
            json_value_free(json_sdp);
            return NULL;
        }
        const char* em_tag = json_object_get_string(json_object(json_entrymap), "tag");
        if (strcmp(em_tag, known_sdp_tags[1]) != 0) {
            fprintf(stderr, "ERROR: Unexpected EntryMap tag '%s'\n", em_tag);
            fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
            json_value_free(json_entrymap);
            json_value_free(json_sdp);
            return NULL;
        }
        json_object_set_value(json_object(json_sdp), "SDP", json_entrymap);

        // Validate the EntryMap and look for the NameMap
        if (hdr->entry_count != 1 || hdr->entry_record_size < sizeof_32(root_entry) / sizeof_32(uint32_t)) {
            fprintf(stderr, "ERROR: Unexpected entry data for a root SDP\n");
            fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
            json_value_free(json_sdp);
            return NULL;
        }

        root_entry* gmpk_root = (root_entry*)&buf[hdr->entry_offset];
        if (data_endianness != platform_endianness) {
            BSWAP_UINT32(gmpk_root->entrymap_offset);
            BSWAP_UINT32(gmpk_root->namemap_offset);
            BSWAP_UINT32(gmpk_root->namemap_size);
            BSWAP_UINT32(gmpk_root->unknown1);
            BSWAP_UINT32(gmpk_root->files_count);
            BSWAP_UINT32(gmpk_root->unknown2);
            BSWAP_UINT32(gmpk_root->max_name_len);
        }
        files_count = gmpk_root->files_count;
        if (gmpk_root->entrymap_offset != hdr->entrymap_offset) {
            fprintf(stderr, "ERROR: EntryMap position mismatch\n");
            fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
            json_value_free(json_sdp);
            return NULL;
        }

        // Process the NameMap
        if (hdr->size - gmpk_root->namemap_offset < gmpk_root->namemap_size) {
            fprintf(stderr, "ERROR: NameMap size is too small\n");
            fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
            json_value_free(json_sdp);
            return NULL;
        }

        JSON_Value* json_nid = read_nid(&buf[gmpk_root->namemap_offset], gmpk_root->namemap_size);
        if (json_nid == NULL) {
            json_value_free(json_sdp);
            return NULL;
        }

        json_object_set_value(json_object(json_sdp), "NID", json_nid);
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

uint32_t write_nid(JSON_Object* json_nid, uint8_t* buf, uint32_t size)
{
    uint32_t *data, written = 0;
    nid1_header* hdr = (nid1_header*)buf;

    if (json_nid == NULL || buf == NULL || size < sizeof_32(nid1_header)) {
        fprintf(stderr, "ERROR: Invalid NID parameters\n");
        return 0;
    }

    // Write the header
    written = sizeof_32(nid1_header);
    const char* tag = json_object_get_string(json_nid, "tag");
    const char* type = json_object_get_string(json_nid, "type");
    if (tag == NULL || type == NULL || strcmp(type, "NID1") != 0 ||
        strcmp(tag, known_nid_tags[0]) != 0) {
        fprintf(stderr, "ERROR: Malformed or missing NID data\n");
        return 0;
    }
    memcpy(hdr->tag, tag, strlen(tag));
    hdr->magic = NID1_LE_MAGIC;
    hdr->max_name_len = 0;

    JSON_Array* json_names_array = json_object_get_array(json_nid, "names");
    hdr->count = (uint32_t)json_array_get_count(json_names_array);
    if (size < sizeof_32(nid1_header) + 3 * hdr->count * sizeof_32(uint32_t)) {
        fprintf(stderr, "ERROR: NID buffer is to small\n");
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
        if (name == NULL)
            return 0xffff;
        len = (uint32_t)strlen(name);
        if (split == 0)
            split = len;
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
        data[2 * hdr->count + i] = getv32(data[2 * hdr->count + i]);
        free(name);
    }
    written += 3 * hdr->count * sizeof_32(uint32_t) + fragments_size;
    written = align_to_4(written);
    hdr->size = written;
    if (data_endianness != platform_endianness) {
        BSWAP_UINT32(hdr->magic);
        BSWAP_UINT32(hdr->size);
        BSWAP_UINT32(hdr->count);
        BSWAP_UINT32(hdr->max_name_len);
    }
    return written;
}

uint32_t write_sdp(JSON_Object* json_sdp, uint8_t* buf, uint32_t size)
{
    sdp1_header* hdr = (sdp1_header*)buf;
    uint32_t* data, written = 0;

    if (json_sdp == NULL || buf == NULL || size < sizeof_32(sdp1_header)) {
        fprintf(stderr, "ERROR: Invalid SDP parameters\n");
        return 0;
    }

    // Write the header
    written = sizeof_32(sdp1_header);
    const char* tag = json_object_get_string(json_sdp, "tag");
    const char* type = json_object_get_string(json_sdp, "type");
    if (tag == NULL || type == NULL || strcmp(type, "SDP1") != 0) {
        fprintf(stderr, "ERROR: Malformed or missing SDP data\n");
        return 0;
    }
    memcpy(hdr->tag, tag, strlen(tag));
    hdr->magic = SDP1_LE_MAGIC;

    // Write the data records
    hdr->data_offset = written;
    data = (uint32_t*)&buf[written];
    JSON_Array* json_data_array = json_object_get_array(json_sdp, "data");
    if (json_array_get_count(json_data_array) == 0) {
        fprintf(stderr, "ERROR: Missing or malformed SDP data\n");
        return 0;
    }
    hdr->data_count = (uint32_t)json_array_get_count(json_data_array) / 2;
    hdr->data_record_size = (uint32_t)json_array_get_count(json_array_get_array(json_data_array, 0));
    written += (uint32_t)json_array_get_count(json_data_array) * hdr->data_record_size * sizeof_32(uint32_t);
    written = align_to_16(written);
    if (written > size) {
        fprintf(stderr, "ERROR: SDP buffer is to small\n");
        return 0;
    }
    for (uint32_t i = 0; i < (uint32_t)json_array_get_count(json_data_array); i++) {
        JSON_Array* json_data_record_array = json_array_get_array(json_data_array, i);
        for (uint32_t j = 0; j < hdr->data_record_size; j++)
            data[i * hdr->data_record_size + j] = getv32(json_array_get_uint32(json_data_record_array, j));
    }

    // Write the entry data
    hdr->entry_offset = written;
    if (strncmp(tag, "GMPK", 4) == 0) {
        // Allocate space for the root entry
        root_entry* gmpk_root = (root_entry*)&buf[hdr->entry_offset];
        hdr->entry_count = 1;
        hdr->entry_record_size = sizeof_32(root_entry) / sizeof_32(uint32_t);
        written += sizeof_32(root_entry);
        written = align_to_16(written);
        if (written > size) {
            fprintf(stderr, "ERROR: SDP buffer is to small\n");
            return 0;
        }
        gmpk_root->entrymap_offset = written;
        JSON_Object* json_entrymap = json_object_get_object(json_sdp, "SDP");
        if (json_entrymap == NULL) {
            fprintf(stderr, "ERROR: EntryMap is missing from root SDP\n");
            return 0;
        }
        uint32_t w = 0;
        if ((w = write_sdp(json_entrymap, &buf[written], size - written)) == 0)
            return 0;
        hdr->entrymap_offset = written;
        written = align_to_16(written + w);

        // An EntryMap should be followed by a NameMap
        gmpk_root->namemap_offset = written;
        JSON_Object* json_namemap = json_object_get_object(json_sdp, "NID");
        if (json_namemap == NULL) {
            fprintf(stderr, "ERROR: NameMap is missing from root SDP\n");
            return 0;
        }
        if ((w = write_nid(json_namemap, &buf[written], size - written)) == 0)
            return 0;
        nid1_header* nid_hdr = (nid1_header*)&buf[written];
        written = align_to_16(written + w);
        gmpk_root->namemap_size = getp32(&nid_hdr->size);
        gmpk_root->unknown1 = 1;
        gmpk_root->files_count = files_count;
        gmpk_root->unknown2 = 1;
        gmpk_root->max_name_len = getp32(&nid_hdr->max_name_len);
        assert(getp32(&buf[gmpk_root->entrymap_offset + 8]) == SDP1_LE_MAGIC);
        assert(getp32(&buf[gmpk_root->namemap_offset + 8]) == NID1_LE_MAGIC);
        assert(getp32(&buf[gmpk_root->namemap_offset + 12]) == gmpk_root->namemap_size);
        assert(getp32(&buf[gmpk_root->namemap_offset + 20]) == gmpk_root->max_name_len);
        if (data_endianness != platform_endianness) {
            BSWAP_UINT32(gmpk_root->entrymap_offset);
            BSWAP_UINT32(gmpk_root->namemap_offset);
            BSWAP_UINT32(gmpk_root->namemap_size);
            BSWAP_UINT32(gmpk_root->unknown1);
            BSWAP_UINT32(gmpk_root->files_count);
            BSWAP_UINT32(gmpk_root->unknown2);
            BSWAP_UINT32(gmpk_root->max_name_len);
        }
    } else if (strcmp(tag, "EntryMap") == 0) {
        // Process EntryMap
        hdr->entry_record_size = 2 * entry_data_size;
        hdr->entry_count = entry_data_count / hdr->entry_record_size;
        written += entry_data_count * sizeof_32(uint32_t);
        written = align_to_16(written);
        if (written > size) {
            fprintf(stderr, "ERROR: SDP buffer is to small\n");
            return 0;
        }
        assert(entry_data != NULL);
        uint32_t* buf_data = (uint32_t*)&buf[hdr->entry_offset];
        for (uint32_t i = 0; i < entry_data_count; i++)
            buf_data[i] = getv32(entry_data[i]);
    } else {
        fprintf(stderr, "ERROR: Unsupported SDP tag '%s'\n", tag);
        return 0;
    }
    hdr->size = written;
    if (data_endianness != platform_endianness) {
        BSWAP_UINT32(hdr->magic);
        BSWAP_UINT32(hdr->size);
        BSWAP_UINT32(hdr->data_count);
        BSWAP_UINT32(hdr->data_record_size);
        BSWAP_UINT32(hdr->entry_count);
        BSWAP_UINT32(hdr->entry_record_size);
        BSWAP_UINT32(hdr->data_offset);
        BSWAP_UINT32(hdr->entry_offset);
        BSWAP_UINT32(hdr->unknown_offset);
        BSWAP_UINT32(hdr->entrymap_offset);
    }
    return written;
}

int main_utf8(int argc, char** argv)
{
    char path[256], *dir = NULL;
    int r = -1;
    JSON_Value* json = NULL;
    FILE *file = NULL;
    uint8_t* buf = NULL;
    bool list_only = (argc == 3) && (argv[1][0] == '-') && (argv[1][1] == 'l');
    bool no_prompt = (argc == 3) && (argv[1][0] == '-') && (argv[1][1] == 'y');

    if ((argc != 2) && !list_only && !no_prompt) {
        printf("%s %s (c) 2021 VitaSmith\n\n"
            "Usage: %s [-l] [-y] <file or directory>\n\n"
            "Extracts (file) or recreates (directory) a Gust .gmpk model pack.\n\n"
            "Note: A backup (.bak) of the original is automatically created, when the target\n"
            "is being overwritten for the first time.\n",
            _appname(argv[0]), GUST_TOOLS_VERSION_STR, _appname(argv[0]));
        return 0;
    }

    if (!is_directory(argv[argc - 1])) {
        // Unpack a GMPK
        printf("%s '%s'...\n", list_only ? "Listing" : "Extracting", argv[argc - 1]);
        size_t len = strlen(argv[argc - 1]);
        if (len < 5 || stricmp(&argv[argc - 1][len - 5], ".gmpk") != 0) {
            fprintf(stderr, "ERROR: File should have a '.gmpk' extension\n");
            goto out;
        }
        char* gmpk_pos = &argv[argc - 1][len - 5];

        uint32_t file_size = read_file(argv[argc - 1], &buf);
        if (file_size == UINT32_MAX)
            goto out;

        if (getle32(buf) != GMPK_MAGIC) {
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

        if (data_endianness == big_endian)
            json_object_set_boolean(json_object(json), "big_endian", true);
        json_object_set_value(json_object(json), "SDP", json_gmpk);

        sdp1_header* gmpk_sdp = (sdp1_header*)buf;
        assert(gmpk_sdp->entrymap_offset != 0);
        sdp1_header* entrymap_sdp = (sdp1_header*)&buf[gmpk_sdp->entrymap_offset];
        assert(entrymap_sdp->entry_record_size >= 4);
        uint32_t* fp = (uint32_t*)&buf[gmpk_sdp->entrymap_offset + entrymap_sdp->entry_offset];
        uint32_t offset = gmpk_sdp->size;
        file_entry* fe = (file_entry*)&buf[offset];
        JSON_Array* json_names = json_object_dotget_array(json_object(json_gmpk), "NID.names");
        if (json_array_get_count(json_names) == 0) {
            fprintf(stderr, "ERROR: NID names array was not found\n");
            goto out;
        }

        printf("OFFSET   SIZE     NAME\n");
        uint32_t extracted_files = 0, num_extensions_to_check = entrymap_sdp->entry_record_size / 2;
        if (entrymap_sdp->entry_count > 1 && getp32(&fp[0]) == 1)
            num_extensions_to_check -= 1;
        if (num_extensions_to_check > array_size(extension)) {
            fprintf(stderr, "ERROR: This archive includes unsupported G1X data\n");
            fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
            goto out;
        }
        for (uint32_t i = 0; i < entrymap_sdp->entry_count; i++, fp = &fp[entrymap_sdp->entry_record_size]) {
            const char* name = json_object_get_string(json_array_get_object(json_names, i), "name");
            for (uint32_t j = 0; j < num_extensions_to_check; j++) {
                if (getp32(&fp[2 * j]) == 1) {
                    uint32_t index = getp32(&fp[2 * j + 1]);
                    // Sanity check
                    if (index > files_count) {
                        fprintf(stderr, "ERROR: File index %d is out of range [0, %d]\n",
                            index, files_count);
                        fprintf(stderr, "Please report this error to %s.\n", REPORT_URL);
                        goto out;
                    }
                    uint32_t fe_offset = getp32(&fe[index].offset);
                    uint32_t fe_size = getp32(&fe[index].size);
                    snprintf(path, sizeof(path), "%s%s%c%s%s", dir,
                       _basename(argv[argc - 1]), PATH_SEP, name, extension[j]);
                    printf("%08x %08x %s%s\n", offset + fe_offset, fe_size, name, extension[j]);
                    // More sanity checks
                    if (offset + fe_offset + fe_size > file_size) {
                        fprintf(stderr, "ERROR: Invalid file size or file offset\n");
                        goto out;
                    }
                    if (extracted_files > files_count) {
                        fprintf(stderr, "ERROR: Invalid number of files\n");
                        goto out;
                    }
                    extracted_files++;
                    if (list_only)
                        continue;
                    FILE* dst = fopen_utf8(path, "wb");
                    if (dst == NULL) {
                        fprintf(stderr, "ERROR: Can't create file '%s'\n", path);
                        goto out;
                    }
                    if (fwrite(&buf[offset + fe_offset], 1, fe_size, dst) != fe_size) {
                        fprintf(stderr, "ERROR: Can't write file '%s'\n", path);
                        fclose(dst);
                        goto out;
                    }
                    fclose(dst);
                }
            }
        }
        if (getp32(&fe[files_count].offset) != file_size) {
            fprintf(stderr, "WARNING: The last file offset doesn't match the total file size\n");
            goto out;
        }
        if (!list_only) {
            snprintf(path, sizeof(path), "%s%cgmpk.json", argv[argc - 1], PATH_SEP);
            json_serialize_to_file_pretty(json, path);
        }
        if (extracted_files != files_count) {
            fprintf(stderr, "ERROR: Some files were not extracted\n");
            goto out;
        }
        r = 0;
    } else {
        // Create a GMPK
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
        if (json_object_get_boolean(json_object(json), "big_endian"))
            data_endianness = big_endian;
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
        dir = strdup(argv[argc - 1]);
        if (dir == NULL) {
            fprintf(stderr, "ERROR: Alloc error\n");
            goto out;
        }
        dir[get_trailing_slash(dir)] = 0;

        JSON_Object* json_gmpk = json_object_get_object(json_object(json), "SDP");
        if (json_gmpk == NULL) {
            fprintf(stderr, "ERROR: Missing JSON root SDP element\n");
            goto out;
        }

        // Create the EntryMap entry data
        JSON_Array* json_names_array = json_object_dotget_array(json_gmpk, "NID.names");
        uint32_t names_count = (uint32_t)json_array_get_count(json_names_array);
        if (names_count == 0 || names_count > MAX_NAMES_COUNT) {
            fprintf(stderr, "ERROR: Invalid/missing NID JSON data\n");
            goto out;
        }
        // Count the number of files we have available
        files_count = 0;
        entry_data_count = 0;
        entry_data = calloc(names_count * sizeof(model_entry), 1);
        if (entry_data == NULL)
            goto out;
        // Note: We expect the EntryMap entry data to be as follows:
        // If we only have one set of .g1m/.g1t/.g1h, there is a single model_entry
        // record of size 4 or 6 since there are no submodels.
        // If we have more than one set of .g1x, we assume that the first one is
        // the main model, with all the other files being its direct descendents.
        // Also, rather than figure out how many separate extensions we have, we
        // cheat by looking at the EntryMap number of records.
        entry_data_size = (uint32_t)json_array_get_count(json_object_dotget_array(json_gmpk, "SDP.data")) / 2;
        if (entry_data_size == 0 || entry_data_size > array_size(extension) + 1) {
            fprintf(stderr, "ERROR: Invalid EntryMap data\n");
            goto out;
        }
        for (size_t i = 0; i < json_array_get_count(json_names_array); i++) {
            const char* name = json_object_get_string(json_array_get_object(json_names_array, i), "name");
            model_entry* me = (model_entry*)&entry_data[entry_data_count];
            for (size_t j = 0; j < array_size(extension); j++) {
                snprintf(path, sizeof(path), "%s%s%c%s%s", dir,
                    _basename(argv[argc - 1]), PATH_SEP, name, extension[j]);
                if (is_file(path)) {
                    me->component[j].has_component = 1;
                    me->component[j].file_index = files_count++;
                }
            }
            if (entry_data[0] == 1 && names_count > 1) {
                me->component[entry_data_size - 1].has_component = 1;
                me->component[entry_data_size - 1].file_index = (i == 0) ? names_count - 1 : 0xffffffff;
            }
            entry_data_count += 2 * entry_data_size;
        }

        // Now create the SDP header
        buf = calloc(MAX_HEADER_SIZE, 1);
        if (buf == NULL)
            goto out;
        uint32_t header_size = write_sdp(json_gmpk, buf, MAX_HEADER_SIZE);
        if (header_size == 0)
            goto out;

        // Create the file entry data section (we will fill the offsets/sizes later)
        file_entry* fe = (file_entry*)&buf[header_size];
        uint32_t fe_size = align_to_16((files_count + 1) * (uint32_t)sizeof(file_entry));
        fwrite(buf, 1, (size_t)header_size + fe_size, file);

        // Add file content to the GMPK
        printf("OFFSET   SIZE     NAME\n");
        uint32_t index = 0;
        uint8_t padding_buf[0x10] = { 0 };
        for (size_t i = 0; i < json_array_get_count(json_names_array); i++) {
            const char* name = json_object_get_string(json_array_get_object(json_names_array, i), "name");
            for (size_t j = 0; j < array_size(extension); j++) {
                snprintf(path, sizeof(path), "%s%s%c%s%s", dir,
                    _basename(argv[argc - 1]), PATH_SEP, name, extension[j]);
                if (is_file(path)) {
                    snprintf(path, sizeof(path), "%s%s%c%s%s", dir,
                        _basename(argv[argc - 1]), PATH_SEP, name, extension[j]);
                    uint8_t* src_buf = NULL;
                    fe[index].offset = ftell(file) - header_size;
                    assert(fe[index].offset % 0x10 == 0);
                    fe[index].size = read_file(path, &src_buf);
                    if (fe[index].size == UINT32_MAX)
                        goto out;
                    printf("%08x %08x %s%s\n", (uint32_t)ftell(file), fe[index].size, name, extension[j]);
                    if (fwrite(src_buf, 1, fe[index].size, file) != fe[index].size) {
                        fprintf(stderr, "ERROR: Can't add data from '%s'\n", path);
                        free(src_buf);
                        goto out;
                    }
                    free(src_buf);
                    // Pad to 16 bytes
                    if (fe[index].size % 0x10 != 0)
                        fwrite(padding_buf, 1, 0x10 - (fe[index].size % 0x10), file);
                    index++;
                }
            }
        }

        // Now overwrite the file entry data
        fe[files_count].offset = ftell(file);
        assert(fe[files_count].offset % 0x10 == 0);
        for (uint32_t i = 0; i <= 2 * files_count; i++)
            ((uint32_t*)fe)[i] = getv32(((uint32_t*)fe)[i]);
        fseek(file, header_size, SEEK_SET);
        if (fwrite(fe, 1, fe_size, file) != fe_size) {
            fprintf(stderr, "ERROR: Can't write file entry data section\n");
            goto out;
        }
        r = 0;
    }

out:
    json_value_free(json);
    free(buf);
    free(dir);
    free(entry_data);
    if (file != NULL)
        fclose(file);

    if (r != 0 && !no_prompt) {
        fflush(stdin);
        printf("\nPress any key to continue...");
        (void)getchar();
    }

    return r;
}

CALL_MAIN

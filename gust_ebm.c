/*
  gust_ebm - Ebm file processor for Gust (Koei/Tecmo) PC games
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

#include "utf8.h"
#include "util.h"
#include "parson.h"

/* An ebm structure is as follows:
    uint32_t type;          // always seems to be set to 2
    uint32_t voice_id;      // id of the voice for the speaking character
    uint32_t unknown1;      // ???
    uint32_t name_id;       // id of the name to use for the speaking character
    uint32_t extra_id;      // seems to be -1 for system messages
    uint32_t expr_id;       // serious = 0x09, surprise = 0x0a, happy = 0x0c, etc.
    uint32_t unknown3;      // [OPTIONAL] Used by Nelke but set to 0xffffffff
    uint32_t unknown4;      // [OPTIONAL] Used by Nelke but set to 0xffffffff
    uint32_t msg_id;        // sequential id of the message
    uint32_t unknown2;      // ???
    uint32_t msg_length;    // length of msg_string
    char     msg_string[0]; // text message to display
    uint32_t extensions;    // [OPTIONAL] NOA2/Ryza2 extensions
 */

int main_utf8(int argc, char** argv)
{
    int r = -1;
    uint8_t* buf = NULL;
    char* ebm_message;
    FILE* file = NULL;
    JSON_Value* json = NULL;

    if (argc != 2) {
        printf("%s %s (c) 2019-2021 VitaSmith\n\n"
            "Usage: %s <file>\n\n"
            "Convert a .ebm file to or from an editable JSON file.\n\n",
            _appname(argv[0]), GUST_TOOLS_VERSION_STR, _appname(argv[0]));
        return 0;
    }

    if (strstr(argv[argc - 1], ".json") != NULL) {
        json = json_parse_file_with_comments(argv[argc - 1]);
        if (json == NULL) {
            fprintf(stderr, "ERROR: Can't parse JSON data from '%s'\n", argv[argc - 1]);
            goto out;
        }
        const char* filename = json_object_get_string(json_object(json), "name");
        printf("Creating '%s' from JSON...\n", filename);
        create_backup(filename);
        file = fopen_utf8(filename, "wb");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Cannot create file '%s'\n", filename);
            goto out;
        }
        bool noa2_extensions = json_object_get_boolean(json_object(json), "noa2_extensions");
        uint32_t header_size = json_object_get_uint32(json_object(json), "header_size");
        if (header_size == 0)
            header_size = 9;
        int32_t nb_messages = (int32_t)json_object_get_uint32(json_object(json), "nb_messages");
        if (fwrite(&nb_messages, sizeof(int32_t), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't write number of messages\n");
            goto out;
        }
        JSON_Array* json_messages = json_object_get_array(json_object(json), "messages");
        if (json_array_get_count(json_messages) != abs(nb_messages)) {
            fprintf(stderr, "ERROR: Number of messages doesn't match the array size\n");
            goto out;
        }
        uint32_t ebm_header[11];
        uint32_t extensions = 0;
        for (int i = 0; i < abs(nb_messages); i++) {
            JSON_Object* json_message = json_array_get_object(json_messages, i);
            memset(ebm_header, 0, sizeof(ebm_header));
            uint32_t j = 0;
            ebm_header[j] = json_object_get_uint32(json_message, "type");
            ebm_header[++j] = json_object_get_uint32(json_message, "voice_id");
            ebm_header[++j] = json_object_get_uint32(json_message, "unknown1");
            ebm_header[++j] = json_object_get_uint32(json_message, "name_id");
            ebm_header[++j] = json_object_get_uint32(json_message, "extra_id");
            ebm_header[++j] = json_object_get_uint32(json_message, "expr_id");
            if (header_size == 11) {
                ebm_header[++j] = 0xffffffff;
                ebm_header[++j] = 0xffffffff;
            }
            ebm_header[++j] = json_object_get_uint32(json_message, "msg_id");
            ebm_header[++j] = json_object_get_uint32(json_message, "unknown2");
            const char* msg_string = json_object_get_string(json_message, "msg_string");
            ebm_header[++j] = (uint32_t)strlen(msg_string) + 1;
            if (fwrite(ebm_header, header_size * sizeof(uint32_t), 1, file) != 1) {
                fprintf(stderr, "ERROR: Can't write message header\n");
                goto out;
            }
            if (fwrite(msg_string, 1, ebm_header[j], file) != ebm_header[j]) {
                fprintf(stderr, "ERROR: Can't write message data\n");
                goto out;
            }
            if (noa2_extensions || json_object_get_boolean(json_message, "padding")) {
                if (noa2_extensions)
                    extensions = json_object_get_uint32(json_message, "extensions");
                if (fwrite(&extensions, sizeof(extensions), 1, file) != 1) {
                    fprintf(stderr, "ERROR: Can't write extensions field\n");
                    goto out;
                }
            }
        }
        r = 0;
    } else if (strstr(argv[argc - 1], ".ebm") != NULL) {
        int noa2_extensions = 0;
        printf("Converting '%s' to JSON...\n", _basename(argv[argc - 1]));
        uint32_t buf_size = read_file(_basename(argv[argc - 1]), &buf);
        if (buf_size == 0)
            goto out;
        int32_t nb_messages = (int32_t)getle32(buf);
        if (buf_size < sizeof(uint32_t) + abs(nb_messages) * sizeof(ebm_message)) {
            fprintf(stderr, "ERROR: Invalid number of entries\n");
            goto out;
        }
        JSON_Value* json_messages = NULL;
        JSON_Value* json_message = NULL;

retry:
        if (noa2_extensions) {
            printf("Detected NOA 2/Ryza 2 ebm extensions...");
            json_value_free(json_message);
            json_value_free(json_messages);
            json_value_free(json);
        }
        // Store the data we'll need to reconstruct the archive to a JSON file
        json = json_value_init_object();
        json_object_set_string(json_object(json), "name", _basename(argv[argc - 1]));
        json_object_set_number(json_object(json), "nb_messages", nb_messages & 0xffffffff);
        json_messages = json_value_init_array();
        uint32_t* ebm_header = (uint32_t*)&buf[sizeof(uint32_t)];
        uint32_t header_size = 0;
        for (int i = 0; i < abs(nb_messages); i++) {
            uint32_t j = 0;
            json_message = json_value_init_object();
            json_object_set_number(json_object(json_message), "type", (double)ebm_header[j]);
            if (ebm_header[j] > 0x10) {
                if (noa2_extensions++)
                    fprintf(stderr, "WARNING: Unexpected header type 0x%08x\n", ebm_header[j]);
                else
                    goto retry;
            }
            json_object_set_number(json_object(json_message), "voice_id", (double)ebm_header[++j]);
            if (ebm_header[++j] != 0)
                json_object_set_number(json_object(json_message), "unknown1", (double)ebm_header[j]);
            json_object_set_number(json_object(json_message), "name_id", (double)ebm_header[++j]);
            if (ebm_header[++j] != 0)
                json_object_set_number(json_object(json_message), "extra_id", (double)ebm_header[j]);
            json_object_set_number(json_object(json_message), "expr_id", (double)ebm_header[++j]);
            if ((ebm_header[++j] == 0xffffffff) && (ebm_header[j + 1] == 0xffffffff)) {
                j += 2;
                if (header_size == 0) {
                    header_size = 11;
                } else if (header_size != 11) {
                    fprintf(stderr, "ERROR: Unexpected header size (Got %d, expected 11)\n", header_size);
                    if (noa2_extensions++ == 0) goto retry;
                    goto out;
                }
            } else {
                if (header_size == 0) {
                    header_size = 9;
                } else if (header_size != 9) {
                    fprintf(stderr, "ERROR: Unexpected header size (Got %d, expected 9)\n", header_size);
                    if (noa2_extensions++ == 0) goto retry;
                    goto out;
                }
            }
            json_object_set_number(json_object(json_message), "msg_id", (double)ebm_header[j]);
            if (ebm_header[++j] != 0)
                json_object_set_number(json_object(json_message), "unknown2", (double)ebm_header[j]);
            // Don't store str_length since we'll reconstruct it
            uint32_t str_length = ebm_header[++j];
            if (str_length > 2048) {
                fprintf(stderr, "ERROR: Unexpected string size\n");
                if (noa2_extensions++ == 0) goto retry;
                goto out;
            }
            char* ptr = (char*)&ebm_header[header_size];
            json_object_set_string(json_object(json_message), "msg_string", ptr);
            ebm_header = (uint32_t*) &ptr[str_length];
            // Some ebm's (e.g. NOA2, Ryza 2) have a 32-bit extension field after the string
            if (noa2_extensions) {
                json_object_set_number(json_object(json_message), "extensions", (double)*ebm_header);
                ebm_header = &ebm_header[1];
            }
            json_array_append_value(json_array(json_messages), json_message);
            json_message = NULL;
        }
        if (noa2_extensions)
            json_object_set_boolean(json_object(json), "noa2_extensions", noa2_extensions);
        json_object_set_number(json_object(json), "header_size", header_size);
        json_object_set_value(json_object(json), "messages", json_messages);
        json_serialize_to_file_pretty(json, change_extension(argv[argc - 1], ".json"));
        r = 0;
    } else {
        fprintf(stderr, "ERROR: You must specify a .ebm or .json file");
    }

out:
    json_value_free(json);
    free(buf);
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

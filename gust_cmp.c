/*
  gust_cmp - Our own binary file comparison, since fc.exe is dismal.
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
#include <stdlib.h>
#include <assert.h>

#include "utf8.h"
#include "util.h"

#define BUFFER_SIZE 65536

static uint64_t compare_files(const char* path1, const char* path2)
{
    uint64_t r = UINT64_MAX, pos = 0;
    uint8_t buf1[BUFFER_SIZE], buf2[BUFFER_SIZE];
    FILE *f1 = NULL, *f2 = NULL;
    f1 = fopen_utf8(path1, "rb");
    if (f1 == NULL) {
        fprintf(stderr, "Cannot open '%s'\n", path1);
        goto out;
    }
    f2 = fopen_utf8(path2, "rb");
    if (f2 == NULL) {
        fprintf(stderr, "Cannot open '%s'\n", path2);
        goto out;
    }

    _fseeki64(f1, 0L, SEEK_END);
    _fseeki64(f2, 0L, SEEK_END);
    if (_ftelli64(f1) != _ftelli64(f2)) {
        fprintf(stderr, "Files differ in size\n");
        goto out;
    }
    _fseeki64(f1, 0L, SEEK_SET);
    _fseeki64(f2, 0L, SEEK_SET);

    while (1) {
        size_t read1 = fread(buf1, 1, BUFFER_SIZE, f1);
        size_t read2 = fread(buf2, 1, BUFFER_SIZE, f2);
        if (read1 != read2) {
            fprintf(stderr, "Read error\n");
            goto out;
        }
        for (size_t i = 0; i < read1; i++) {
            if (buf1[i] != buf2[i]) {
                r = pos + i;
                fprintf(stderr, "Files differ at offset 0x%09llx\n", r);
                goto out;
            }
        }
        if ((feof(f1) != 0) && (feof(f2) != 0))
            break;
    }
    r = 0;

out:
    if (f1 != NULL)
        fclose(f1);
    if (f2 != NULL)
        fclose(f2);
    return r;
}

int main_utf8(int argc, char** argv)
{
    if (argc != 3) {
        printf("%s %s (c) 2019-2021 VitaSmith\n\n"
            "Usage: %s <file2> <file2>\n\n"
            "Coompare two binary files.\n\n",
            _appname(argv[0]), GUST_TOOLS_VERSION_STR, _appname(argv[0]));
        return -1;
    }

    return (compare_files(argv[1], argv[2]) == 0) ? 0 : -1;
}

CALL_MAIN

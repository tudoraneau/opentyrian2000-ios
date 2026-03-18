/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "file.h"

#include "opentyr.h"
#include "varz.h"

#include "SDL.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef TYRIAN_DIR
#define TYRIAN_DIR NULL
#endif

#ifndef ARRAY_COUNT
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#endif

const char *custom_data_dir = NULL;

// finds the Tyrian data directory
const char *data_dir(void)
{
	const char *const dirs[] =
	{
		custom_data_dir,
		TYRIAN_DIR,
		"data",
		".",
	};

	static const char *dir = NULL;

	if (dir != NULL)
		return dir;

#ifdef __IPHONEOS__
        // On iOS, read game data from Documents/Tyrian20 so that users
        // can manage the files via the Files app without bundling them.
        static char ios_documents_path[1024] = "";

        const char *home = getenv("HOME");
        if (home)
        {
                snprintf(ios_documents_path, sizeof(ios_documents_path),
                         "%s/Documents/Tyrian20", home);
        }

        const char *const ios_dirs[] = {
                custom_data_dir,
                ios_documents_path,
        };

        for (size_t i = 0; i < sizeof(ios_dirs) / sizeof(ios_dirs[0]); ++i)
        {
                if (!ios_dirs[i] || ios_dirs[i][0] == '\0')
                        continue;

                FILE *f = dir_fopen(ios_dirs[i], "tyrian1.lvl", "rb");
                if (f)
                {
                        fclose(f);
                        dir = ios_dirs[i];
                        break;
                }
        }
#else
        for (size_t i = 0; i < ARRAY_COUNT(dirs); ++i)
        {
                if (dirs[i] == NULL)
                        continue;

                FILE *f = dir_fopen(dirs[i], "tyrian1.lvl", "rb");
                if (f)
                {
                        fclose(f);

                        dir = dirs[i];
                        break;
                }
        }
#endif

	if (dir == NULL) // data not found
		dir = "";

	return dir;
}

// prepend directory and fopen
FILE *dir_fopen(const char *dir, const char *file, const char *mode)
{
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);
    int needs_slash = (dir_len > 0 && dir[dir_len - 1] != '/');
    size_t path_len = dir_len + (needs_slash ? 1 : 0) + file_len;
    char *path = malloc(path_len + 1);
    if (!path) {
        return NULL;
    }
    if (needs_slash) {
        sprintf(path, "%s/%s", dir, file);
    } else {
        sprintf(path, "%s%s", dir, file);
    }

    FILE *f = fopen(path, mode);

    // On case-sensitive filesystems (iOS), try uppercase filename if
    // lowercase failed — original Tyrian 2000 ships UPPERCASE files.
    if (!f)
    {
        size_t fname_offset = dir_len + (needs_slash ? 1 : 0);
        for (size_t i = fname_offset; i < path_len; ++i)
            path[i] = toupper((unsigned char)path[i]);
        f = fopen(path, mode);
    }

    free(path);

    return f;
}

// warn when dir_fopen fails
FILE *dir_fopen_warn(const char *dir, const char *file, const char *mode)
{
	FILE *f = dir_fopen(dir, file, mode);

	if (f == NULL)
		fprintf(stderr, "warning: failed to open '%s': %s\n", file, strerror(errno));

	return f;
}

// die when dir_fopen fails
FILE *dir_fopen_die(const char *dir, const char *file, const char *mode)
{
	FILE *f = dir_fopen(dir, file, mode);

	if (f == NULL)
	{
		fprintf(stderr, "error: failed to open '%s': %s\n", file, strerror(errno));
		fprintf(stderr, "error: One or more of the required Tyrian " TYRIAN_VERSION " data files could not be found.\n"
		                "       Please read the README file.\n");
		JE_tyrianHalt(1);
	}

	return f;
}

// check if file can be opened for reading
bool dir_file_exists(const char *dir, const char *file)
{
	FILE *f = dir_fopen(dir, file, "rb");
	if (f != NULL)
		fclose(f);
	return (f != NULL);
}

// returns end-of-file position
long ftell_eof(FILE *f)
{
	long pos = ftell(f);

	fseek(f, 0, SEEK_END);
	long size = ftell(f);

	fseek(f, pos, SEEK_SET);

	return size;
}

void fread_die(void *buffer, size_t size, size_t count, FILE *stream)
{
	long pos = ftell(stream);
	size_t result = fread(buffer, size, count, stream);
	if (result != count)
	{
		fprintf(stderr, "error: fread_die failed at offset %ld, requested %zu*%zu, got %zu (feof=%d ferror=%d)\n",
		        pos, size, count, result, feof(stream), ferror(stream));
		SDL_Quit();
		exit(EXIT_FAILURE);
	}
}

void fwrite_die(const void *buffer, size_t size, size_t count, FILE *stream)
{
	size_t result = fwrite(buffer, size, count, stream);
	if (result != count)
	{
		fprintf(stderr, "error: An unexpected problem occurred while writing to a file.\n");
		SDL_Quit();
		exit(EXIT_FAILURE);
	}
}


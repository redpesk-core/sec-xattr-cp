/*
 * Copyright (C) 2015-2026 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * $RP_BEGIN_LICENSE$
 * Commercial License Usage
 *  Licensees holding valid commercial IoT.bzh licenses may use this file in
 *  accordance with the commercial license agreement provided with the
 *  Software or, alternatively, in accordance with the terms contained in
 *  a written agreement between you and The IoT.bzh Company. For licensing terms
 *  and conditions see https://www.iot.bzh/terms-conditions. For further
 *  information use the contact form at https://www.iot.bzh/contact.
 *
 * GNU General Public License Usage
 *  Alternatively, this file may be used under the terms of the GNU General
 *  Public license version 3. This license is as published by the Free Software
 *  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
 *  of this file. Please review the following information to ensure the GNU
 *  General Public License requirements will be met
 *  https://www.gnu.org/licenses/gpl-3.0.html.
 * $RP_END_LICENSE$
 */

#include "common.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>

/* root of strings */
struct recstr *recstrs = NULL;

/* root of entries */
struct recentry *root = NULL;

/* array for reading lines */
char buffer[4 * 65536];

struct recentry *addent(struct recentry **pentries, char *path)
{
	size_t pos;
	char save;
	struct recentry *curent, *nxtent;

	/* remove heading slashes */
	for (; *path == '/' ; path++);
	if (*path == 0)
		return NULL; /* at end */

	/* find end of path component */
	for (pos = 0 ; path[pos] && path[pos] != '/' ; pos++);
	if (pos == 1 && path[0] == '.')
		/* when component is . do as if not existing */
		return addent(pentries, &path[pos]);

	/* add the component */
	save = path[pos];
	path[pos] = 0;
	curent = add_recentry(pentries, &recstrs, path, pos + 1);

	/* add sub components if possible */
	path[pos] = save;
	nxtent = addent(&curent->subs, &path[pos]);

	/* return the last component */
	return nxtent != NULL ? nxtent : curent;
}

static inline
size_t skip_spaces(size_t pos)
{
	while (buffer[pos] == ' ' || buffer[pos] == '\t')
		pos++;
	return pos;
}

static inline
size_t skip_no_spaces(size_t pos)
{
	while (buffer[pos] && buffer[pos] != ' ' && buffer[pos] != '\t')
		pos++;
	return pos;
}

void addline()
{
	size_t begpath, endpath, begname, endname, begval, endval, end;
	struct recentry *entry;

	begpath = skip_spaces(0);
	endpath = skip_no_spaces(begpath);

	begname = skip_spaces(endpath);
	endname = skip_no_spaces(begname);

	begval = skip_spaces(endname);
	endval = skip_no_spaces(begval);

	end = skip_spaces(endval);

	if (buffer[end] || endval == begval) {
		fprintf(stderr, "ignore line %s\n", buffer);
		return;
	}

	endpath = begpath + unescape(&buffer[begpath], endpath - begpath);
	endname = begname + unescape(&buffer[begname], endname - begname);
	endval = begval + unescape(&buffer[begval], endval - begval);
	if (endval - begval > UINT16_MAX) {
		fprintf(stderr, "attribute value too long\n");
		return;
	}

	buffer[endpath] = 0;
	entry = addent(&root, &buffer[begpath]);
	if (entry != NULL) {
		buffer[endname] = 0;
		add_recattr(
			&entry->attr, &recstrs,
			&buffer[begname], 1 + endname - begname,
			&buffer[begval], endval - begval);
	}
}

/* read lines for input file and process it */
void build(FILE *file)
{
	for (;;) {
		size_t idx = 0;
		for (;;) {
			int c = getc(file);
			if (c == EOF && idx == 0)
				return;
			if (c == EOF || c == '\n')
				break;
			buffer[idx++] = (char)c;
			if (idx == sizeof buffer) {
				fprintf(stderr, "line too long!!!\n");
				exit(EXIT_FAILURE);
			}
		}
		buffer[idx] = 0;
		addline();
	}
}

void usage(char **av)
{
	printf("usage: %s FILE\n", av[0]);
	exit(EXIT_FAILURE);
}

int main(int ac, char **av)
{
	/* check argument count */
	if (2 != ac)
       		usage(av);

	/* build the data */
	build(stdin);

	/* write */
	write_attr_file(av[1], root, recstrs);

	return(EXIT_SUCCESS);
}


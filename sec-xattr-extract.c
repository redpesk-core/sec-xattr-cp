/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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
#include <regex.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/xattr.h>

/* root of strings */
struct recstr *recstrs = NULL;

/* root of entries */
struct recentry *root = NULL;

/* array for listing attribute names */
char lstattr[65536];

/* array for getting attribute values */
char valattr[65535];

/* current path */
char path[PATH_MAX];

/* should dump? */
bool dump = false;

/* should process pattern */
bool pattern = false;
regex_t rex;

/* root device */
unsigned long rootdev;


/* extend the path */
void addpath(size_t pos, const char *str, size_t len)
{
	if (pos + len > sizeof path) {
		fprintf(stderr, "file too long %.*s%.*s\n", (int)pos, path, (int)len, str);
		exit(EXIT_FAILURE);
	}
	memcpy(&path[pos], str, len);
}

/* scan the entry referenced by path, the basename starting at pos and being of len */
void extr_entry(struct recentry **pentries, size_t pos, size_t len)
{
	struct recentry *entry;
	size_t szattr, idx, szval, anlen;
	ssize_t rc;

	/* get the list of attributes */
	rc = llistxattr(path, lstattr, sizeof lstattr);
	if (rc < 0) {
		fprintf(stderr, "Can't get attributes of file %s: %s\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	szattr = (size_t)rc;
	if (rc > sizeof lstattr) {
		fprintf(stderr, "too much attributes for file %s: %s\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* nothing to do if empty */
	if (szattr == 0)
		return;

	/* iterate the attributes */
	entry = NULL;
	for (idx = 0 ; idx < szattr ; idx += anlen + 1) {

		/* check the attribute name */
		anlen = strlen(&lstattr[idx]);
		if (pattern && regexec(&rex, &lstattr[idx], 0, NULL, 0))
			continue;

		/* get/create the entry on need */
		if (entry == NULL)
			entry = add_recentry(pentries, &recstrs, &path[pos], len + 1);

		/* get the value */
		rc = lgetxattr(path, &lstattr[idx], valattr, sizeof valattr);
		if (rc < 0) {
			fprintf(stderr, "Can't get attribute %s of file %s: %s\n",
					       &lstattr[idx], path, strerror(errno));
			exit(EXIT_FAILURE);
		}
		szval = (size_t)rc;
		if (szval > sizeof valattr || szval > UINT16_MAX) {
			fprintf(stderr, "too big attribute %s in file %s: %s\n",
					       &lstattr[idx], path, strerror(errno));
			exit(EXIT_FAILURE);
		}

		/* record the attribute in the entry */
		add_recattr(&entry->attr, &recstrs, &lstattr[idx], anlen + 1, valattr, szval);

		/* dump */
		if (dump) {
			printstr(stdout, path, strlen(path));
			fprintf(stdout, "\t");
			printstr(stdout, &lstattr[idx], anlen);
			fprintf(stdout, "\t");
			printstr(stdout, &valattr[2], szval);
			fprintf(stdout, "\n");
		}
	}
}

/* extract attributes from current directory in path */
void extr_dir(struct recentry **pentries, size_t pos, bool root)
{
	struct dirent *ent;
	struct recentry *subs;
	DIR *dir;
	size_t len;
	struct stat st;

	/* open the directory */
	dir = opendir(path);
	if (dir == NULL) {
		fprintf(stderr, "Failed to open directory %s: %s\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (pos == 0 || path[pos - 1] != '/')
		addpath(pos++, "/", 1);

	/* loop on each entry */
	while (ent = readdir(dir)) {

		/* length of file */

		/* avoid . and .. */
		if (strcmp(ent->d_name, "..") == 0)
			continue;
		if (!root && strcmp(ent->d_name, ".") == 0)
			continue;

		/* copy name */
		len = strlen(ent->d_name);
		addpath(pos, ent->d_name, len + 1);

		/* extract the entry */
		extr_entry(pentries, pos, len);

		/* enter sub directories */
		if (ent->d_type == DT_DIR && strcmp(ent->d_name, ".") != 0) {
			if (stat(path, &st) < 0) {
				fprintf(stderr, "Can't stat %s: %s\n", path, strerror(errno));
				exit(EXIT_FAILURE);
			}
			if (st.st_dev == rootdev) {
				subs = NULL;
				extr_dir(&subs, pos + len, false);
				/* create the entry only if needed */
				if (subs != NULL) {
					path[pos + len] = 0;
					add_recentry(pentries, &recstrs, &path[pos], len + 1)->subs = subs;
				}
			}
		}
	}
	closedir(dir);
}

/* extract from root path rpath */
void extract(const char *rpth)
{
	struct stat st;
	size_t len = strlen(rpth);
	int rc = stat(rpth, &st);
	if (rc < 0) {
		fprintf(stderr, "Can't stat %s: %s\n", rpth, strerror(errno));
		exit(EXIT_FAILURE);
	}
	rootdev = st.st_dev;
	addpath(0, rpth, len);
	extr_dir(&root, len, true);
}

void set_pattern(const char *pat)
{
	int rc = regcomp(&rex, pat, REG_EXTENDED|REG_NOSUB);
	if (rc != 0) {
		fprintf(stderr, "Can't compile pattern %s: %d\n", pat, rc);
		exit(EXIT_FAILURE);
	}
	pattern = true;
}

void usage(char **av)
{
	printf("usage: %s [-d] [-m pattern] FILE ROOT\n", av[0]);
	exit(EXIT_FAILURE);
}

void main(int ac, char **av)
{
	int idx = 1;

	/* get options */
	while (idx < ac && av[idx][0] == '-') {
		if (strcmp(av[idx], "-d") == 0)
			dump = true;
		else if (strcmp(av[idx], "-m") == 0)
			set_pattern(av[++idx]);
		else
			usage(av);
		idx++;
	}

	/* check argument count */
	if (idx + 2 != ac)
       		usage(av);

	/* process the root */
	extract(av[idx + 1]);

	/* write */
	write_attr_file(av[idx], root, recstrs);

	exit(EXIT_SUCCESS);
}


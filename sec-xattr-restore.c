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

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <endian.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>

#include "sec-xattr-cp.h"

char path[PATH_MAX];

#if WITHOUT_DRY_RUN

# define APPLY lsetxattr

#else

# define APPLY apply

int (*apply)(const char *path, const char *name, const void *value, size_t size, int flags)
	= lsetxattr;

int dry_apply(const char *path, const char *name, const void *value, size_t size, int flags)
{
	fprintf(stdout, "%s\t%s\t%.*s\n", path, name, size, (const char*)value);
}

#endif

void *process(uint32_t *pcode, size_t offset, const char *subpath)
{
	static const char *attr = NULL;

	const char *str;
	uint32_t code;
	int rc;
	size_t len;

	/* append the subpath */
	len = strlen(subpath);
	if (offset + len > sizeof path) {
		fprintf(stderr, "path too long %.*s%s\n", offset, path, subpath);
		exit(EXIT_FAILURE);
	}
	memcpy(&path[offset], subpath, len);
	offset += len;

	/* append the trailing slash */
	if (offset == 0 || path[offset - 1] != '/') {
		if (offset + 1 > sizeof path) {
			fprintf(stderr, "path too long %.*s/\n", offset, path);
			exit(EXIT_FAILURE);
		}
		path[offset++] = '/';
	}

	/* iterate over instructions */
	for (;;) {
		code = *pcode++;
		code = le32toh(code);
		str = &((char*)pcode)[code >> TAG_WIDTH];
		switch (code & TAG_MASK) {
		case TAG_SUB:
			if (code == TAG_SUB) /* offset == 0 */
				return pcode;
			pcode = process(pcode, offset, str);
			break;
		case TAG_FILE:
			len = strlen(str) + 1;
			if (offset + len > sizeof path) {
				fprintf(stderr, "path too long %.*s%s\n", offset, path, offset);
				exit(EXIT_FAILURE);
			}
			memcpy(&path[offset], str, len);
			break;
		case TAG_ATTR:
			attr = str;
			break;
		case TAG_SET:
			len = ((size_t)(uint8_t)str[0]) | (((size_t)(uint8_t)str[1]) << 8);
			rc = APPLY(path, attr, &str[2], len, 0);
			if (rc < 0) {
				fprintf(stderr, "can't set %s of %s\n", attr, path);
				exit(EXIT_FAILURE);
			}
			break;
		}
	}
}

/*
 * Opens the file 'path' and map it in memory.
 * Returns the position in memory
 */
void *mapin(const char *path)
{
	int rc, fd;
	struct stat st;
	void *ptr;

	/* open the file */
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* gets its properties */
	rc = fstat(fd, &st);
	if (rc < 0) {
		fprintf(stderr, "failed to stat %s: %s\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* check it is a regular file */
	if ((st.st_mode & S_IFMT) != S_IFREG) {
		fprintf(stderr, "%s should be a regular file\n", path);
		exit(EXIT_FAILURE);
	}

	/* map the regular file in memory */
	ptr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr == MAP_FAILED) {
		fprintf(stderr, "failed to mmap %s: %s\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* check header */
	if (memcmp(ptr, SEC_XATTR_CP_ID_V1, strlen(SEC_XATTR_CP_ID_V1)) != 0) {
		fprintf(stderr, "%s isn't of expected format\n", path);
		exit(EXIT_FAILURE);
	}

	/* return the values */
	return (void*)(((char*)ptr) + strlen(SEC_XATTR_CP_ID_V1));
}

void main(int ac, char **av)
{
	uint32_t *ptr;

#if WITHOUT_DRY_RUN
	/* check argument count */
	if (ac != 3) {
		fprintf(stderr, "usage: %s FILE ROOT\n", av[0]);
		exit(EXIT_FAILURE);
	}
#else
	/* check argument count */
	if (ac == 4 && strcmp(av[1], "-d") == 0) {
		apply = dry_apply;
		av++;
	}
	else if (ac != 3) {
		fprintf(stderr, "usage: %s [-d] FILE ROOT\n", av[0]);
		exit(EXIT_FAILURE);
	}
#endif

	/* map the file */
	ptr = mapin(av[1]);

	/* process the root */
	process(ptr, 0, av[2]);

	exit(EXIT_SUCCESS);
}


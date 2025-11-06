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
#include <stdio.h>
#include <string.h>
#include <limits.h>

uint32_t *base;
char spaces[] = "                              "
                "                              "
                "                              "
                "                              "
                "                              "
                "                              "
                "                              "
                "                              "
                "                              "
                "                              ";

char path[PATH_MAX];

void *process(uint32_t *pcode, unsigned depth, size_t offset, const char *subpath)
{
	static const char *attr = NULL;

	const char *str;
	uint32_t code;
	int rc, tag, off, dep;
	size_t len;

	/* append the subpath */
	len = strlen(subpath);
	if (offset + len > sizeof path) {
		fprintf(stderr, "path too long %.*s%s\n", (int)offset, path, subpath);
		exit(EXIT_FAILURE);
	}
	memcpy(&path[offset], subpath, len);
	offset += len;

	/* append the trailing slash */
	if (offset == 0 || path[offset - 1] != '/') {
		if (offset + 1 > sizeof path) {
			fprintf(stderr, "path too long %.*s/\n", (int)offset, path);
			exit(EXIT_FAILURE);
		}
		path[offset++] = '/';
	}
	fprintf(stdout, "%06d %.*s",(int)((pcode-base)*sizeof*pcode), 3*depth, spaces);
	fprintf(stdout, "ENTERING %.*s\n", (int)offset, path);

	/* iterate over instructions */
	for (;;) {
		code = *pcode;
		code = le32toh(code);
		tag = (int)(code & TAG_MASK);
		off = (int)(code >> TAG_WIDTH);
		fprintf(stdout, "%06d %.*s",(int)((pcode-base)*sizeof*pcode), 3*depth, spaces);
		str = &((char*)++pcode)[code >> TAG_WIDTH];
		dep = (int)(str - (char*)base);
		switch (code & TAG_MASK) {
		case TAG_SUB:
			if (code == TAG_SUB) { /* offset == 0 */
				fprintf(stdout, "END\n");
				return pcode;
			}
			fprintf(stdout, "SUB %d=%d %s\n", off, dep, str);
			pcode = process(pcode, depth + 1, offset, str);
			break;
		case TAG_FILE:
			len = strlen(str) + 1;
			if (offset + len > sizeof path) {
				fprintf(stderr, "path too long %.*s%s\n", (int)offset, path, offset);
				exit(EXIT_FAILURE);
			}
			memcpy(&path[offset], str, len);
			fprintf(stdout, "FILE %d=%d %s\n", off, dep, str);
			fprintf(stdout, "       %.*s", 3*depth, spaces);
			fprintf(stdout, "  -> %s\n", path);
			break;
		case TAG_ATTR:
			fprintf(stdout, "ATTR %d=%d %s\n", off, dep, str);
			attr = str;
			break;
		case TAG_SET:
			len = ((size_t)(uint8_t)str[0]) | (((size_t)(uint8_t)str[1]) << 8);
			fprintf(stdout, "SET  %d=%d %d %.*s\n", off, dep, (int)len, len, &str[2]);
			if (rc < 0) {
				fprintf(stderr, "can't set %s of %s\n", attr, path);
				exit(EXIT_FAILURE);
			}
			break;
		}
	}
}

void main(int ac, char **av)
{

	/* check argument count */
	if (ac != 3) {
		fprintf(stderr, "usage: %s FILE ROOT\n", av[0]);
		exit(EXIT_FAILURE);
	}

	/* map the file */
	base = mapin(av[1]);

	/* process the root */
	process(base, 0, 0, av[2]);

	exit(EXIT_SUCCESS);
}


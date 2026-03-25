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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/xattr.h>

extern char **environ;

int dry_apply(const char *path, const char *name, const void *value, size_t size, int flags)
{
	dump_entry(stdout, path, name, value, size);
	return 0;
}

void usage(char **av)
{
	fprintf(stderr, "usage: %s [-d] FILE ROOT [program [arg ...]]\n" , av[0]);
	exit(EXIT_FAILURE);
}

int main(int ac, char **av)
{
	int i0 = 1;
	int (*apply)(const char *path, const char *name, const void *value, size_t size, int flags)
		= lsetxattr;

	if (ac > 1 && strcmp(av[1], "-d") == 0) {
		apply = dry_apply;
		i0++;
	}

	/* check argument count */
	if (ac < i0 + 2)
		usage(av);

	/* restore the attributes */
	apply_attr_file(av[i0], av[i0 + 1], apply);

	/* check for existing command */
	i0 += 2;
	if (ac <= i0)
		exit(EXIT_SUCCESS);

	/* execute the given command */
	execve(av[i0], &av[i0], environ);
	fprintf(stderr, "can't exec %s: %s\n", av[i0], strerror(errno));
	return(EXIT_FAILURE);
}


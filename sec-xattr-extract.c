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
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <endian.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <regex.h>

#include "sec-xattr-cp.h"

/* record a string */
struct recstr {
	size_t size;        /* size of the string without zero */
	struct recstr *nxt; /* next string record */
	size_t offset;      /* final offset in file */
	char value[];       /* the string terminated with a zero */
};

/* record the setting of an attribute */
struct recattr {
	struct recattr *nxt;   /* next setting for the same entry */
	struct recstr  *name;  /* string for the name of the attribute */
	struct recstr  *value; /* string for the value of the attribute */
};

/* record the setting for an entry */
struct recentry {
	struct recstr   *name; /* string for the name of the entry */
	struct recentry *nxt;  /* next entry */
	struct recattr  *attr; /* list of attributes if any */
	struct recentry *subs; /* list of entries for directories */
};

/* root of strings */
struct recstr *recstrs = NULL;

/* root of entries */
struct recentry *root = NULL;

/* record of the current attribute name */
struct recstr *curattr;

/* array for listing attribute names */
char lstattr[65536];

/* array for getting attribute values and their prefixed length */
char valattr[2 + 65535];

/* current path */
char path[PATH_MAX];

/* should dump? */
bool dump = false;

/* should process pattern */
bool pattern = false;
regex_t rex;

/* allocation of memory */
void *alloc(size_t sz)
{
	void *result = malloc(sz);
	if (result == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(EXIT_FAILURE);
	}
	return result;
}

/* write the file */
void wr(int fd, const void *ptr, size_t sz)
{
	ssize_t rc = write(fd, ptr, sz);
	if (rc < 0) {
		if (errno != EINTR) {
			fprintf(stderr, "write error\n");
			exit(EXIT_FAILURE);
		}
		wr(fd, ptr, sz);
	}
}

/* extend the path */
void addpath(size_t pos, const char *str, size_t len)
{
	if (pos + len > sizeof path) {
		fprintf(stderr, "file too long %.*s%.*s\n", pos, path, len, str);
		exit(EXIT_FAILURE);
	}
	memcpy(&path[pos], str, len);
}

/* return the string record for the given string */
struct recstr *addstr(const char *value, size_t sz)
{
	/* search */
	struct recstr **prv = &recstrs;
	struct recstr *iter = recstrs;
	while (iter != NULL && !(iter->size == sz && 0 == memcmp(value, iter->value, sz))) {
		prv = &iter->nxt;
		iter = iter->nxt;
	}
	if (iter == NULL) {
		/* create if not found */
		*prv = iter = alloc(sz + sizeof *iter);
		iter->size = sz;
		memcpy(iter->value, value, sz);
		iter->nxt = NULL;
		iter->offset = 0;
	}
	return iter;
}

/* compute the offsets of strings */
void set_str_offsets(size_t initial)
{
	size_t offset = initial;
	struct recstr *iter = recstrs;
	while(iter != NULL) {
		iter->offset = offset;
		offset += iter->size;
		iter = iter->nxt;
	}
}

/* write the strings */
void write_str(int fd, size_t offset)
{
	struct recstr *iter = recstrs;
	if (iter != NULL) {
		if (iter->offset != offset) {
			fprintf(stderr, "internal error, string offset mismatch %lu and %lu\n",
					(unsigned long)offset, (unsigned long)iter->offset);
			exit(EXIT_FAILURE);
		}
		while(iter != NULL) {
			wr(fd, iter->value, iter->size);
			iter = iter->nxt;
		}
	}
}

/* create and add an attribute record */
void add_attr(struct recattr **phead, const char *name, size_t lenname, const char *value, size_t lenvalue)
{
	struct recattr *attr = alloc(sizeof *attr);
	attr->nxt = NULL;
	attr->name = addstr(name, lenname);
	attr->value = addstr(value, lenvalue);
	while(*phead != NULL)
		phead = &(*phead)->nxt;
	*phead = attr;
}

/* get the entry for the given name zero terminated,
 * the length len must include the ending zero */
struct recentry *add_entry(struct recentry **phead, const char *str, size_t len)
{
	/* search the entry in the list referenced by phead */
	struct recstr *name = addstr(str, len);
	struct recentry *iter = *phead;
	while (iter && iter->name != name) {
		phead = &iter->nxt;
		iter = iter->nxt;
	}
	if (iter == NULL) {
		/* not found, create it at end */
		*phead = iter = alloc(sizeof *iter);
		iter->name = name;
		iter->nxt = NULL;
		iter->attr = NULL;
		iter->subs = NULL;
	}
	return iter;
}

/* scan the entry referenced by path, the basename starting at pos and being of len */
void extr_entry(struct recentry **phead, size_t pos, size_t len)
{
	struct recentry *entry;
	size_t szattr, idx, szval;
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
	for (idx = 0 ; idx < szattr ; idx += len + 1) {

		/* check the attribute name */
		len = strlen(&lstattr[idx]);
		if (pattern && regexec(&rex, &lstattr[idx], 0, NULL, 0))
			continue;

		/* get/create the entry on need */
		if (entry == NULL)
			entry = add_entry(phead, &path[pos], len + 1);

		/* get the value */
		rc = lgetxattr(path, &lstattr[idx], &valattr[2], sizeof valattr - 2);
		if (rc < 0) {
			fprintf(stderr, "Can't get attribute %s of file %s: %s\n",
					       &lstattr[idx], path, strerror(errno));
			exit(EXIT_FAILURE);
		}
		szval = (size_t)rc;
		if (szval > sizeof valattr - 2 || szval > UINT16_MAX) {
			fprintf(stderr, "too big attribute %s in file %s: %s\n",
					       &lstattr[idx], path, strerror(errno));
			exit(EXIT_FAILURE);
		}

		/* record the attribute in the entry */
		if (dump)
			printf("%s\t%s\t%.*s\n", path, &lstattr[idx], szval, &valattr[2]);
		valattr[0] = (char)(uint8_t)(szval & 255);
		valattr[1] = (char)(uint8_t)((szval >> 8) & 255);
		add_attr(&entry->attr, &lstattr[idx], len + 1, valattr, szval + 2);
	}
}

/* extract attributes from current directory in path */
void extr_dir(struct recentry **phead, size_t pos, bool root)
{
	struct dirent *ent;
	struct recentry *subs;
	DIR *dir;
	size_t len;

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
		addpath(pos,  ent->d_name, 1 + len);

		/* extract the entry */
		extr_entry(phead, pos, len);

		/* enter sub directories */
		if (ent->d_type == DT_DIR && strcmp(ent->d_name, ".") != 0) {
			subs = NULL;
			extr_dir(&subs, pos + len, false);
			/* create the entry only if needed */
			if (subs != NULL) {
				path[pos + len] = 0;
				add_entry(phead, &path[pos], len + 1)->subs = subs;
			}
		}
	}
	closedir(dir);
}

/* extract from root path rpath */
void extract(const char *rpth)
{
	size_t len = strlen(rpth);
	addpath(0, rpth, len);
	extr_dir(&root, len, true);
}

/* put the operation being at offset and return the offset of the next operation */
size_t putop(int fd, size_t offset, uint32_t op, struct recstr *str)
{
	/* offset of next */
	offset += sizeof(uint32_t);
	if (fd >= 0) {
		/* argument of op */
		if (str != NULL)
			op |= (((uint32_t)(str->offset - offset)) << TAG_WIDTH);
		/* write it */
		op = htole32(op);
		wr(fd, &op, sizeof op);
	}
	return offset;
}

/* write operations for entry starting at offset and return the offset after */
size_t write_ops(struct recentry *entry, size_t offset, int fd)
{
	struct recattr *attr;
	/* write the entry's ops */
	while (entry != NULL) {
		/* enter subdirectory if needed */
		if (entry->subs) {
			offset = putop(fd, offset, TAG_SUB, entry->name);
			offset = write_ops(entry->subs, offset, fd);
		}
		/* write attributes if any */
		attr = entry->attr;
		if (attr != NULL) {
			offset = putop(fd, offset, TAG_FILE, entry->name);
			while (attr != NULL) {
				if (attr->name != curattr) {
					offset = putop(fd, offset, TAG_ATTR, attr->name);
					curattr = attr->name;
				}
				offset = putop(fd, offset, TAG_SET, attr->value);
				attr = attr->nxt;
			}
		}
		/* next */
		entry = entry->nxt;
	}
	return putop(fd, offset, TAG_SUB, NULL);
}

void prepare()
{
	size_t offset;

	offset = strlen(SEC_XATTR_CP_ID_V1);
	curattr = NULL;
	offset = write_ops(root, offset, -1);
	set_str_offsets(offset);
}

void write_file(const char *path)
{
	size_t offset;
	/* open / create the file */
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		fprintf(stderr, "Can't open file %s: %s\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	/* write the header */
	offset = strlen(SEC_XATTR_CP_ID_V1);
	wr(fd, SEC_XATTR_CP_ID_V1, offset);
	/* write the operations */
	curattr = NULL;
	offset = write_ops(root, offset, fd);
	/* write the strings */
	write_str(fd, offset);
	/* end */
	close(fd);
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
	printf("usage: %s [-d] [-m pattern] FILE ROOT\n");
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

	/* prepare */
	prepare();

	/* write */
	write_file(av[idx]);

	exit(EXIT_SUCCESS);
}


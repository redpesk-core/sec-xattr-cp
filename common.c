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
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <endian.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* allocation of memory */
static void *alloc(size_t sz)
{
	void *result = malloc(sz);
	if (result == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(EXIT_FAILURE);
	}
	return result;
}

/* return the string record for the given string */
struct recstr *add_recstr(struct recstr **pstrings, const char *value, size_t sz)
{
	/* search */
	struct recstr **prv = pstrings;
	struct recstr *iter = *pstrings;
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

/* return the string record for the given value */
struct recstr *add_recval(struct recstr **pstrings, const char *value, size_t sz)
{
	/* search */
	struct recstr **prv = pstrings;
	struct recstr *iter = *pstrings;
	size_t szl = sz + 2;
	char c0 = (char)(uint8_t)(sz & 255);
	char c1 = (char)(uint8_t)((sz >> 8) & 255);

	while (iter != NULL && (iter->size != szl || iter->value[0] != c0 || iter->value[1] != c1
			       	|| memcmp(value, &iter->value[2], sz) != 0)) {
		prv = &iter->nxt;
		iter = iter->nxt;
	}

	if (iter == NULL) {
		/* create if not found */
		*prv = iter = alloc(szl + sizeof *iter);
		iter->size = szl;
		iter->value[0] = c0;
		iter->value[1] = c1;
		memcpy(&iter->value[2], value, sz);
		iter->nxt = NULL;
		iter->offset = 0;
	}
	return iter;
}

/* create and add an attribute record */
void add_recattr(struct recattr **pattrs, struct recstr **pstrings, const char *name, size_t lenname, const char *value, size_t lenvalue)
{
	struct recattr *attr = alloc(sizeof *attr);
	attr->nxt = NULL;
	attr->name = add_recstr(pstrings, name, lenname);
	attr->value = add_recval(pstrings, value, lenvalue);
	while(*pattrs != NULL)
		pattrs = &(*pattrs)->nxt;
	*pattrs = attr;
}

/* get the entry for the given name string */
struct recentry *add_recentry_str(struct recentry **pentries, struct recstr *name)
{
	/* search the entry in the list referenced by pentries */
	struct recentry *iter = *pentries;
	while (iter && iter->name != name) {
		pentries = &iter->nxt;
		iter = iter->nxt;
	}
	if (iter == NULL) {
		/* not found, create it at end */
		*pentries = iter = alloc(sizeof *iter);
		iter->name = name;
		iter->nxt = NULL;
		iter->attr = NULL;
		iter->subs = NULL;
	}
	return iter;
}

/* get the entry for the given name zero terminated,
 * the length len must include the ending zero */
struct recentry *add_recentry(struct recentry **pentries, struct recstr **pstrings, const char *str, size_t len)
{
	return add_recentry_str(pentries, add_recstr(pstrings, str, len));
}

/******************************************************************************
* manage escaped strings
******************************************************************************/

void printstr(FILE *file, const char *value, size_t lenvalue)
{
	size_t idx = 0;
	while (idx < lenvalue) {
		char c = value[idx++];
		if (c <= 32 || c == '\\' || c >= 127)
			fprintf(file, "\\%03o", (int)(unsigned char)c);
		else
			fprintf(file, "%c", c);
	}
}

void dump_entry(FILE *file, const char *path, const char *name, const void *value, size_t size)
{
	printstr(file, path, strlen(path));
	fprintf(file, "\t");
	printstr(file, name, strlen(name));
	fprintf(file, "\t");
	printstr(file, value, size);
	fprintf(file, "\n");
}

size_t unescape(char *buffer, size_t len)
{
	size_t idx = 0, iwr = 0;
	while (idx < len) {
		char c = buffer[idx++];
		if (c == '\\' && idx + 2 < len
		 && buffer[idx] >= '0' && buffer[idx] <= '3'
		 && buffer[idx + 1] >= '0' && buffer[idx + 1] <= '7'
		 && buffer[idx + 2] >= '0' && buffer[idx + 2] <= '7') {
			c = (char)( ((buffer[idx + 0] - '0') << 6)
			          | ((buffer[idx + 1] - '0') << 3)
			          |  (buffer[idx + 2] - '0'));
			idx += 3;
		}
		buffer[iwr++] = c;
	}
	return iwr;
}

/******************************************************************************
* creation of the binary file
******************************************************************************/

/* write the file */
static void wr(int fd, const void *ptr, size_t sz)
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

/* write the strings */
static void write_str(struct recstr *strings, size_t offset, int fd)
{
	struct recstr *iter = strings;
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

/* put the operation being at offset and return the offset of the next operation */
static size_t putop(int fd, size_t offset, uint32_t op, struct recstr *str)
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
static size_t write_ops(struct recentry *entry, size_t offset, struct recstr **curattr, int fd)
{
	struct recattr *attr;
	/* write the entry's ops */
	while (entry != NULL) {
		/* enter subdirectory if needed */
		if (entry->subs) {
			offset = putop(fd, offset, TAG_SUB, entry->name);
			offset = write_ops(entry->subs, offset, curattr, fd);
		}
		/* write attributes if any */
		attr = entry->attr;
		if (attr != NULL) {
			offset = putop(fd, offset, TAG_FILE, entry->name);
			while (attr != NULL) {
				if (attr->name != *curattr) {
					offset = putop(fd, offset, TAG_ATTR, attr->name);
					*curattr = attr->name;
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

/* compute the offsets of strings */
static void set_str_offsets(struct recstr *strings, size_t initial)
{
	size_t offset = initial;
	struct recstr *iter = strings;
	while(iter != NULL) {
		iter->offset = offset;
		offset += iter->size;
		iter = iter->nxt;
	}
}

static void prepare(struct recentry *root, struct recstr *strings)
{
	size_t offset;
	struct recstr *curattr = NULL;

	offset = strlen(SEC_XATTR_CP_ID_V1);
	offset = write_ops(root, offset, &curattr, -1);
	set_str_offsets(strings, offset);
}

void write_attr_file(const char *path, struct recentry *root, struct recstr *strings)
{
	size_t offset;
	int fd;
	struct recstr *curattr = NULL;

	/* prepare */
	prepare(root, strings);

	/* open / create the file */
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		fprintf(stderr, "Can't open file %s: %s\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* write the header */
	offset = strlen(SEC_XATTR_CP_ID_V1);
	wr(fd, SEC_XATTR_CP_ID_V1, offset);
	/* write the operations */
	offset = write_ops(root, offset, &curattr, fd);
	/* write the strings */
	write_str(strings, offset, fd);
	/* end */
	close(fd);
}

/******************************************************************************
* reading file
******************************************************************************/

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

struct process {
	const char *attr;
	int (*apply)(const char *path, const char *name, const void *value, size_t size, int flags);
	char path[PATH_MAX];
};

static size_t addpath(struct process *proc, size_t offset, const char *subpath, size_t len)
{
	if (offset + len > sizeof proc->path) {
		fprintf(stderr, "path too long %.*s%s\n", (int)offset, proc->path, subpath);
		exit(EXIT_FAILURE);
	}
	memcpy(&proc->path[offset], subpath, len);
	return offset + len;
}

static void *process(uint32_t *pcode, struct process *proc, size_t offset, const char *subpath)
{
	const char *str;
	uint32_t code;
	int rc;
	size_t len;

	/* append the subpath */
	offset = addpath(proc, offset, subpath, strlen(subpath));

	/* append the trailing slash */
	if (offset == 0 || proc->path[offset - 1] != '/')
		offset = addpath(proc, offset, "/", 1);

	/* iterate over instructions */
	for (;;) {
		code = *pcode++;
		code = le32toh(code);
		str = &((char*)pcode)[code >> TAG_WIDTH];
		switch (code & TAG_MASK) {
		case TAG_SUB:
			if (code == TAG_SUB) /* offset == 0 */
				return pcode;
			pcode = process(pcode, proc, offset, str);
			break;
		case TAG_FILE:
			addpath(proc, offset, str, strlen(str) + 1);
			break;
		case TAG_ATTR:
			proc->attr = str;
			break;
		case TAG_SET:
			len = ((size_t)(uint8_t)str[0]) | (((size_t)(uint8_t)str[1]) << 8);
			rc = proc->apply(proc->path, proc->attr, &str[2], len, 0);
			if (rc < 0) {
				fprintf(stderr, "can't set %s of %s\n", proc->attr, proc->path);
				exit(EXIT_FAILURE);
			}
			break;
		}
	}
}

void apply_attr_file(const char *path, const char *prefix, int (*apply)(const char *path, const char *name, const void *value, size_t size, int flags))
{
	void *ptr;
	struct process proc;

	/* init processing */
	proc.attr = NULL;
	proc.apply = apply;

	/* map the file */
	ptr = mapin(path);

	/* process the root */
	process(ptr, &proc, 0, prefix);
}

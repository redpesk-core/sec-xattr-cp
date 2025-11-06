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
#pragma once

#include <stddef.h>
#include <stdio.h>

#define SEC_XATTR_CP_ID_V1 "sec-xattr-cp 1\n\n"

#define TAG_WIDTH 2
#define TAG_MASK  ((1 << TAG_WIDTH) - 1)
#define TAG_SUB   0
#define TAG_FILE  1
#define TAG_ATTR  2
#define TAG_SET   3

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


struct recstr *add_recstr(struct recstr **pstrings, const char *value, size_t sz);

struct recstr *add_recval(struct recstr **pstrings, const char *value, size_t sz);

void add_recattr(struct recattr **pattrs, struct recstr **pstrings, const char *name, size_t lenname, const char *value, size_t lenvalue);


struct recentry *add_recentry(struct recentry **pentries, struct recstr **pstrings, const char *str, size_t len);


void write_attr_file(const char *path, struct recentry *root, struct recstr *strings);


void printstr(FILE *file, const char *value, size_t lenvalue);

void dump_entry(FILE *file, const char *path, const char *name, const void *value, size_t size);

size_t unescape(char *buffer, size_t len);

void *mapin(const char *path);

void apply_attr_file(const char *path, const char *prefix, int (*apply)(const char *path, const char *name, const void *value, size_t size, int flags));

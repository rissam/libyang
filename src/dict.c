/**
 * @file dict.c
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @brief libyang dictionary for storing strings
 *
 * Copyright (c) 2015 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 */

#include <string.h>
#include <stdint.h>
#include <stdlib.h>


#include "common.h"
#include "dict.h"

struct dict_rec {
	char *value;
	uint32_t refcount;
	struct dict_rec *next;
};

#define DICT_SIZE 1024
struct dict_table {
	struct dict_rec recs[DICT_SIZE];
	int hash_mask;
	uint32_t used;
};
static struct dict_table dict = {{{ 0 }}, DICT_SIZE - 1, 0};

/*
 * Bob Jenkin's one-at-a-time hash
 * http://www.burtleburtle.net/bob/hash/doobs.html
 *
 * Spooky hash is faster, but it works only for little endian architectures.
 */
static uint32_t dict_hash(const char *key, size_t len)
{
    uint32_t hash, i;

    for(hash = i = 0; i < len; ++i)
    {
        hash += key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

struct dict_rec *dict_lookup(const char *value, size_t len)
{
	uint32_t index;
	struct dict_rec *record;

	index = dict_hash(value, len) & dict.hash_mask;

	if (!dict.recs[index].value) {
		/* no such record */
		return NULL;
	}
	record = &dict.recs[index];

	if (!memcmp(value, record->value, len) && record->value[len] == '\0') {
		return record;
	}

	return NULL;
}

void dict_remove(const char *value)
{
	size_t len;
	uint32_t index;
	struct dict_rec *record, *prev = NULL;

	if (!value) {
		return;
	}

	len = strlen(value);

	index = dict_hash(value, len) & dict.hash_mask;
	record = &dict.recs[index];

	while (record && record->value != value) {
		prev = record;
		record = record->next;
	}

	if (!record) {
		/* record not found */
		return;
	}

	record->refcount--;
	if (!record->refcount) {
		free(record->value);
		if (record->next) {
			if (prev) {
				/* change in dynamically allocated chain */
				prev = record->next;
				free(record);
			} else {
				/* move dynamically allocated record into the static array */
				prev = record->next; /* temporary storage */
				memcpy(record, record->next, sizeof *record);
				free(prev);
			}
		} else if (prev) {
			/* removing last record from the dynamically allocated chain */
			prev->next = NULL;
			free(record);
		}
	}
}

char *dict_insert(const char *value, size_t len)
{
	uint32_t index;
	struct dict_rec *record, *new;

	if (!value || len == 0) {
		return NULL;
	}

	index = dict_hash(value, len) & dict.hash_mask;
	record = &dict.recs[index];

	if (!record->value) {
		/* first record with this hash */
		record->value = malloc((len + 1) * sizeof *record->value);
		memcpy(record->value, value, len);
		record->value[len] = '\0';
		record->refcount = 1;
		record->next = NULL;

		LY_VRB("DICT: inserting \"%s\"", record->value);
		return record->value;
	}

	/* collision, search if the value is already in dict */
	while (record) {
		if (!memcmp(value, record->value, len) && record->value[len] == '\0') {
			/* record found */
			record->refcount++;
			LY_VRB("DICT: inserting (refcount) \"%s\"", record->value);
			return record->value;
		}

		if (!record->next) {
			/* not present, add as a new record in chain */
			break;
		}

		record = record->next;
	}

	/* create new record and add it behind the record */
	new = malloc(sizeof *record);
	new->value = malloc((len + 1) * sizeof *record->value);
	memcpy(new->value, value, len);
	new->value[len] = '\0';
	new->refcount = 1;

	record->next = new;

	LY_VRB("DICT: inserting \"%s\" with collision ", record->value);
	return new->value;
}

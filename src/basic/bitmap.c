/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2015 Tom Gundersen

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "alloc-util.h"
#include "bitmap.h"
#include "hashmap.h"
#include "macro.h"

struct Bitmap {
        uint64_t *bitmaps;
        size_t n_bitmaps;
        size_t bitmaps_allocated;
};

/* Bitmaps are only meant to store relatively small numbers
 * (corresponding to, say, an enum), so it is ok to limit
 * the max entry. 64k should be plenty. */
#define BITMAPS_MAX_ENTRY 0xffff

/* This indicates that we reached the end of the bitmap */
#define BITMAP_END ((unsigned) -1)

#define BITMAP_NUM_TO_OFFSET(n)           ((n) / (sizeof(uint64_t) * 8))
#define BITMAP_NUM_TO_REM(n)              ((n) % (sizeof(uint64_t) * 8))
#define BITMAP_OFFSET_TO_NUM(offset, rem) ((offset) * sizeof(uint64_t) * 8 + (rem))

Bitmap *bitmap_new(void) {
        return new0(Bitmap, 1);
}

Bitmap *bitmap_copy(Bitmap *b) {
        Bitmap *ret;

        ret = bitmap_new();
        if (!ret)
                return NULL;

        ret->bitmaps = newdup(uint64_t, b->bitmaps, b->n_bitmaps);
        if (!ret->bitmaps)
                return mfree(ret);

        ret->n_bitmaps = ret->bitmaps_allocated = b->n_bitmaps;
        return ret;
}

void bitmap_free(Bitmap *b) {
        if (!b)
                return;

        free(b->bitmaps);
        free(b);
}

int bitmap_ensure_allocated(Bitmap **b) {
        Bitmap *a;

        assert(b);

        if (*b)
                return 0;

        a = bitmap_new();
        if (!a)
                return -ENOMEM;

        *b = a;

        return 0;
}

int bitmap_set(Bitmap *b, unsigned n) {
        uint64_t bitmask;
        unsigned offset;

        assert(b);

        /* we refuse to allocate huge bitmaps */
        if (n > BITMAPS_MAX_ENTRY)
                return -ERANGE;

        offset = BITMAP_NUM_TO_OFFSET(n);

        if (offset >= b->n_bitmaps) {
                if (!GREEDY_REALLOC0(b->bitmaps, b->bitmaps_allocated, offset + 1))
                        return -ENOMEM;

                b->n_bitmaps = offset + 1;
        }

        bitmask = UINT64_C(1) << BITMAP_NUM_TO_REM(n);

        b->bitmaps[offset] |= bitmask;

        return 0;
}

void bitmap_unset(Bitmap *b, unsigned n) {
        uint64_t bitmask;
        unsigned offset;

        if (!b)
                return;

        offset = BITMAP_NUM_TO_OFFSET(n);

        if (offset >= b->n_bitmaps)
                return;

        bitmask = UINT64_C(1) << BITMAP_NUM_TO_REM(n);

        b->bitmaps[offset] &= ~bitmask;
}

bool bitmap_isset(Bitmap *b, unsigned n) {
        uint64_t bitmask;
        unsigned offset;

        if (!b)
                return false;

        offset = BITMAP_NUM_TO_OFFSET(n);

        if (offset >= b->n_bitmaps)
                return false;

        bitmask = UINT64_C(1) << BITMAP_NUM_TO_REM(n);

        return !!(b->bitmaps[offset] & bitmask);
}

bool bitmap_isclear(Bitmap *b) {
        unsigned i;

        if (!b)
                return true;

        for (i = 0; i < b->n_bitmaps; i++)
                if (b->bitmaps[i] != 0)
                        return false;

        return true;
}

void bitmap_clear(Bitmap *b) {

        if (!b)
                return;

        b->bitmaps = mfree(b->bitmaps);
        b->n_bitmaps = 0;
        b->bitmaps_allocated = 0;
}

bool bitmap_iterate(Bitmap *b, Iterator *i, unsigned *n) {
        uint64_t bitmask;
        unsigned offset, rem;

        assert(i);
        assert(n);

        if (!b || i->idx == BITMAP_END)
                return false;

        offset = BITMAP_NUM_TO_OFFSET(i->idx);
        rem = BITMAP_NUM_TO_REM(i->idx);
        bitmask = UINT64_C(1) << rem;

        for (; offset < b->n_bitmaps; offset ++) {
                if (b->bitmaps[offset]) {
                        for (; bitmask; bitmask <<= 1, rem ++) {
                                if (b->bitmaps[offset] & bitmask) {
                                        *n = BITMAP_OFFSET_TO_NUM(offset, rem);
                                        i->idx = *n + 1;

                                        return true;
                                }
                        }
                }

                rem = 0;
                bitmask = 1;
        }

        i->idx = BITMAP_END;

        return false;
}

bool bitmap_equal(Bitmap *a, Bitmap *b) {
        size_t common_n_bitmaps;
        Bitmap *c;
        unsigned i;

        if (a == b)
                return true;

        if (!a != !b)
                return false;

        if (!a)
                return true;

        common_n_bitmaps = MIN(a->n_bitmaps, b->n_bitmaps);
        if (memcmp(a->bitmaps, b->bitmaps, sizeof(uint64_t) * common_n_bitmaps) != 0)
                return false;

        c = a->n_bitmaps > b->n_bitmaps ? a : b;
        for (i = common_n_bitmaps; i < c->n_bitmaps; i++)
                if (c->bitmaps[i] != 0)
                        return false;

        return true;
}

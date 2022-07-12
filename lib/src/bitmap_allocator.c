#include "orbit.h"
#include "orbit_lowlevel.h"
#include "bitmap_allocator.h"
#include "private/bitmap_allocator_private.h"

#include <string.h>
#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>

#if 0
#define obprintf(fmt, ...) do { fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)
#else
#define obprintf(...) do { } while (0)
#endif

/* FIXME: We now assumes page size of 4096 and processor to be 64-bits.
 * Ideally we should dynamically plan bitmap size using page size from
 * `getpagesize()`, and use `unsigned long`. */
/* FIXME: Currently we only provide an alignment of 8.  This may not work
 * if the user stores SIMD structures. */

#define BLOCK_SIZE 32
#define PAGE_SIZE 4096
#define BLOCKS_PER_PAGE (PAGE_SIZE / BLOCK_SIZE)

/* TODO: make this into a private include file */
#define _define_round_up(base) \
	static inline size_t round_up_##base(size_t value) { \
		return (value + base - 1) & ~(base - 1); \
	}
#define _define_round_down(base) \
	static inline size_t round_down_##base(size_t value) { \
		return value & ~(base - 1); \
	}

_define_round_up(32)
_define_round_up(4096)
_define_round_down(4096)
#define round_down_page round_down_4096
#define round_up_page round_up_4096
#define round_up_block round_up_32

#undef _define_round_up
#undef _define_round_down

typedef unsigned long long bitmap_t;
typedef unsigned char byte;

#define ctz(_x) ({ bitmap_t x = (_x); x == 0 ? 64 : __builtin_ctzll(x); })
#define clz(_x) ({ bitmap_t x = (_x); x == 0 ? 64 : __builtin_clzll(x); })

/* The following three functions: if input bits == 0 or shift == 64,
 * output is undefined */
static inline bitmap_t bitmap_mask(int bits, int shift) {
        return (~0ULL >> (64 - bits)) << shift;
}
static inline bitmap_t lsb_mask(int bits) {
        return ~0ULL >> (64 - bits);
}
static inline bitmap_t msb_mask(int bits) {
        return ~0ULL << (64 - bits);
}

static inline size_t min(size_t x, size_t y) {
	return x < y ? x : y;
}
static inline size_t max(size_t x, size_t y) {
	return x > y ? x : y;
}

/* Page metadata. Including bitmap and some others. */
struct page_meta {
	pthread_spinlock_t lock;	/* Page level lock (maybe not used) */
	unsigned int used;		/* Used blocks in page */
	bitmap_t bitmap[2];		/* Bitmap of free blocks */
	byte free_count[8];		/* Used for optimization */
};

/* Structure of the bitmap allocator information.
 * This resides at the beginning of the pool.  The actual page used for
 * allocation starts after all the page metadata. */
struct orbit_bitmap_allocator {
	struct orbit_allocator base;	/* Allocator base struct, includes vtable */
	pthread_spinlock_t lock;	/* Global lock (maybe not used) */
	byte *first_page;		/* Start of data pages */
	size_t npages;			/* Number of pages */
	size_t allocated;		/* Last page that contains data */
	size_t *data_length;		/* External data length field to be updated */
	struct page_meta page_meta[];	/* Metadata of all pages */
};

extern const struct orbit_allocator_vtable orbit_bitmap_allocator_vtable;

/* Allocation record header before each allocated data. */
struct alloc_header {
	size_t blocks;
};

const size_t header_size = sizeof(struct alloc_header);
const size_t meta_size = sizeof(struct page_meta);
const size_t allocator_size = sizeof(struct orbit_bitmap_allocator);

static inline int page_meta_init(struct page_meta *meta)
{
	memset(meta, 0, sizeof(*meta));
	meta->free_count[7] = 1;
	return pthread_spin_init(&meta->lock, PTHREAD_PROCESS_PRIVATE);
}

struct orbit_allocator *orbit_bitmap_allocator_create(void *start, size_t size,
		void **data_start, size_t *data_length,
		const struct orbit_allocator_method *method)
{
	size_t npages;
	size_t i;
	int ret;
	(void)method;

	size = (byte*)round_down_page((size_t)start + size) - (byte*)start;
	npages = (size - allocator_size) / (meta_size + PAGE_SIZE);

	struct orbit_bitmap_allocator *alloc = (struct orbit_bitmap_allocator*)start;

	ret = pthread_spin_init(&alloc->lock, PTHREAD_PROCESS_PRIVATE);
	if (ret != 0) return NULL;

	alloc->base.vtable = &orbit_bitmap_allocator_vtable;
	alloc->first_page = (byte*)start + round_up_page(
			allocator_size + npages * meta_size);
	alloc->npages = npages;
	alloc->allocated = 0;
	alloc->data_length = data_length;

	for (i = 0; i < npages; ++i)
		if (page_meta_init(alloc->page_meta + i))
			return NULL;

	if (data_start)
		*data_start = alloc->first_page;
	if (data_length)
		*data_length = 0;

	return (struct orbit_allocator*)alloc;
}

void orbit_bitmap_allocator_destroy(struct orbit_allocator *base)
{
	struct orbit_bitmap_allocator *alloc = (struct orbit_bitmap_allocator*)base;
	size_t i;
	pthread_spin_lock(&alloc->lock);
	pthread_spin_destroy(&alloc->lock);
	for (i = 0; i < alloc->npages; ++i)
		pthread_spin_destroy(&alloc->page_meta[i].lock);
}

/* Helper function to clear bits. Bits range should be in [0,128). Though
 * going out of this range won't have bad effects. */
static inline void bitmap_clear_bits(struct page_meta *meta, size_t offset, size_t count)
{
	if (offset < 64) {
		meta->bitmap[0] &= ~bitmap_mask(min(count, 64 - offset), offset);
		if (offset + count > 64)
			meta->bitmap[1] &= ~bitmap_mask(offset + count - 64, 0);
	} else {
		meta->bitmap[1] &= ~bitmap_mask(count, offset - 64);
	}
}

/* Helper function to set bits. Bits range should be in [0,128). Though
 * going out of this range won't have bad effects. */
static inline void bitmap_set_bits(struct page_meta *meta, size_t offset, size_t count)
{
	if (offset < 64) {
		meta->bitmap[0] |= bitmap_mask(min(count, 64 - offset), offset);
		if (offset + count > 64)
			meta->bitmap[1] |= bitmap_mask(offset + count - 64, 0);
	} else {
		meta->bitmap[1] |= bitmap_mask(count, offset - 64);
	}
}

/* Helper function to clear whole bitmap */
static inline void bitmap_clear(struct page_meta *meta) {
	meta->bitmap[1] = meta->bitmap[0] = 0;
}
/* Helper function to set whole bitmap */
static inline void bitmap_set(struct page_meta *meta) {
	meta->bitmap[1] = meta->bitmap[0] = ~0ULL;
}

/* Helper function to count consecutive zeros at the beginning
 * of a page across both bitmaps. */
static inline size_t bitmap_zeros_beginning(struct page_meta *meta) {
	int first = ctz(meta->bitmap[0]);
	int last = first == 64 ? ctz(meta->bitmap[1]) : 0;
	return first + last;
}

/* Helper function to count consecutive zeros at the end
 * of a page across both bitmaps. */
static inline size_t bitmap_zeros_end(struct page_meta *meta) {
	obprintf("clz bitmap=%lld, clz=%d\n", meta->bitmap[1], clz(meta->bitmap[1]));
	int last = clz(meta->bitmap[1]);
	int first = last == 64 ? clz(meta->bitmap[0]) : 0;
	return first + last;
}

/*
static inline size_t multi_page_zeros_after()
{
} */

/* Helper function to find consecutive zeros in one bitmap.
 * On success, return k. The k-th to (k+bits-1)-th bits are set.
 * On fail, return -1. */
static inline int find_zeros_and_set(bitmap_t *bitmapp, int bits)
{
	/* Mask with `bits` ones */
	bitmap_t mask = lsb_mask(bits);
	/* Copy of bitmap */
	bitmap_t bitmap = *bitmapp;
	int k;

	/*
	 * Starting k from the first zero in the bitmap.  To find the hole,
	 * use ctz(~bitmap).
	 *
	 * We always maintain that all bits before k in bitmap are set to 1.
	 * This makes it easier to find the next hole.
	 * In each iteration, if the test fails, we set all zeros in the hole
	 * to ones.  The hole is guaranteed to be smaller than `bits`,
	 * otherwise the test will not fail.
	 *
	 * Example:
	 *   bitmap is  000010110011 (LSB is the first bit)
	 *   bits is 3
	 * Loop trace:
	 *   1st iteration:
	 *     k =  ctz(111101001100) = 2
	 *     Test:    000010110011 \
	 *            & 000000011100 != 0
	 *     bitmap = 000010111111
	 *   2nd iteration:
	 *     k =  ctz(111101000000) = 6
	 *     Test:    000010111111 \
	 *            & 000111000000 != 0
	 *     bitmap = 000011111111
	 *   3rd iteration:
	 *     k =  ctz(111100000000) = 8
	 *     Test:    000011111111
	 *            & 011100000000 == 0
	 * *bitmapp =   000010110011
	 *            & 011100000000
	 *            = 011110110011
	 * Worst case scenario would be many pairs of 10 and one 00, bits=3.
	 */
	for (k = ctz(~bitmap); k + bits <= 64;
		bitmap |= bitmap_mask(ctz(bitmap >> k), k), k = ctz(~bitmap))
	{
		if ((bitmap & (mask << k)) == 0) {
			*bitmapp |= (mask << k);
			return k;
		}
	}

	return -1;
}

/* Helper function to find consecutive zeros from the end of the first bitmap
 * to the start of second bitmap.
 * On success, return k. The k-th to (k+bits-1)-th bits are set.
 * On fail, return -1. */
static inline int find_edge_zeros_and_set(bitmap_t *bitmap0, bitmap_t *bitmap1, int bits)
{
	obprintf("bitmap0=0x%016llx bitmap1=0x%016llx\n", *bitmap0, *bitmap1);
	/* Zeros at the end of bitmap0 (MSB as last bits) */
	int zeros0 = clz(*bitmap0);
	/* Zeros at the beginning of bitmap1 (LSB as first bits) */
	int zeros1 = ctz(*bitmap1);
	obprintf("zeros0=%d, zeros1=%d, bits=%d\n", zeros0, zeros1, bits);

	if (zeros0 + zeros1 < bits)
		return -1;
	zeros1 = bits - zeros0;

	obprintf("mask0=0x%016llx mask1=0x%016llx\n", msb_mask(zeros0), lsb_mask(zeros1));
	*bitmap0 |= msb_mask(zeros0);
	*bitmap1 |= lsb_mask(zeros1);
	obprintf("bitmap0=0x%016llx bitmap1=0x%016llx\n", *bitmap0, *bitmap1);
	return 64 - zeros0;
}

static inline int bitmap_find_zeros_and_set(struct page_meta *meta, int bits)
{
	int k;
	k = find_zeros_and_set(meta->bitmap, bits);
	if (k != -1) return k;
	k = find_zeros_and_set(meta->bitmap + 1, bits);
	if (k != -1) return k + 64;
	return find_edge_zeros_and_set(meta->bitmap, meta->bitmap + 1, bits);
}

/* Helper function to find pages */
/* static size_t bitmap_find_pages(struct orbit_bitmap_allocator *alloc, size_t npages)
{
} */

void *orbit_black_hole(void *ptr) { return ptr; }

void *orbit_bitmap_alloc(struct orbit_allocator *base, size_t size)
{
	struct orbit_bitmap_allocator *alloc = (struct orbit_bitmap_allocator*)base;
	size_t blocks;
	struct page_meta *meta;
	struct alloc_header *header = NULL;
	size_t start_page, end_page, start_block;

	if (!alloc || size == 0) return NULL;

	size = round_up_block(size + header_size);
	blocks = size / BLOCK_SIZE;

	pthread_spin_lock(&alloc->lock);
	obprintf("Allocating %lu bytes %lu blocks, accum %lu\n", size, blocks, alloc->allocated);

	if (size > PAGE_SIZE) {
		/* Represent # of blocks needed as mP + nB where m is # of Pages and n is # of Blocks */
		size_t m = size / PAGE_SIZE;
		size_t n = (size % PAGE_SIZE) / BLOCK_SIZE;
		size_t found_enough_pages;
		obprintf("m %lu, n %lu\n", m, n);

		for (size_t pagei = 0; pagei + m < alloc->npages;) {
			start_page = pagei;
			end_page = pagei + m;

			/* Look for gaps in page before and page after */
			size_t before_blocks = bitmap_zeros_end(alloc->page_meta + start_page);
			obprintf("start page %lu, before_blocks %lu\n", start_page, before_blocks);
			size_t after_blocks = bitmap_zeros_beginning(alloc->page_meta + end_page);
			obprintf("end page %lu, after_blocks %lu\n", end_page, after_blocks);
			if (before_blocks + after_blocks < BLOCKS_PER_PAGE + n) {
				pagei = end_page;
				continue;
			}

			/* Find m - 1 consecutive empty pages */
			found_enough_pages = 1;
			for (size_t midpage = start_page + 1; midpage < end_page; ++midpage) {
				if (alloc->page_meta[midpage].used) {
					obprintf("page %lu, used %u\n", midpage,
						alloc->page_meta[midpage].used);
					pagei = midpage;
					found_enough_pages = 0;
					break;
				}
			}
			if (!found_enough_pages)
				continue;

			/* Set bits */
			bitmap_set_bits(alloc->page_meta + end_page, 0, after_blocks);
			alloc->page_meta[end_page].used += after_blocks;

			for (size_t midpage = end_page - 1; midpage > start_page; --midpage) {
				meta = alloc->page_meta + midpage;
				bitmap_set(meta);
				meta->used = BLOCKS_PER_PAGE;
			}

			start_block = after_blocks - n;
			bitmap_set_bits(alloc->page_meta + start_page, start_block,
				BLOCKS_PER_PAGE - start_block);
			alloc->page_meta[start_page].used += BLOCKS_PER_PAGE - start_block;

			goto success;
		}
	} else {
		for (size_t pagei = 0; pagei < alloc->npages; ++pagei) {
			meta = alloc->page_meta + pagei;

			if (BLOCKS_PER_PAGE - meta->used < blocks)
				continue;

			int k = bitmap_find_zeros_and_set(meta, blocks);
			if (k == -1)
				continue;

			meta->used += blocks;

			start_page = pagei;
			end_page = pagei;
			start_block = k;
			goto success;
		}
	}

	fprintf(stderr, "Allocation failed!\n");
	abort();
	goto exit;

success:
	if (end_page + 1 > alloc->allocated) {
		alloc->allocated = end_page + 1;
		if (alloc->data_length)
			*alloc->data_length = (end_page + 1) * PAGE_SIZE;
	}
	header = (struct alloc_header*)(alloc->first_page
			+ start_page * PAGE_SIZE + start_block * BLOCK_SIZE);
	header->blocks = blocks;
	obprintf("Allocated at page=%lu block=%lu ptr=%p request=%lu B\n",
		start_page, start_block, header + 1, blocks);

exit:
	orbit_black_hole(header);
	pthread_spin_unlock(&alloc->lock);
	return header ? header + 1 : NULL;
}

/* Helper function to convert ptr in the pool to pagei and blocki.
 * If this page does not exist in the pool, -1 is returned.
 * Otherwise, this function will modify the pagei and blocki pointers, and
 * return 0. */
static inline int translate(struct orbit_bitmap_allocator *alloc, void *ptr,
		size_t *pagei, size_t *blocki)
{
	size_t offset = (byte*)ptr - alloc->first_page;
	if ((byte*)ptr < alloc->first_page || offset / PAGE_SIZE >= alloc->npages)
		return -1;
	*pagei = offset / PAGE_SIZE;
	*blocki = (offset % PAGE_SIZE) / BLOCK_SIZE;
	return 0;
}

void orbit_bitmap_free(struct orbit_allocator *base, void *ptr)
{
	struct orbit_bitmap_allocator *alloc = (struct orbit_bitmap_allocator*)base;
	struct alloc_header *header;
	size_t pagei, blocki;
	size_t blocks, blocks_this_page;
	struct page_meta *meta;

	if (!alloc || !ptr) return;

	header = (struct alloc_header*)ptr - 1;
	if (translate(alloc, ptr, &pagei, &blocki))
		return;
	blocks = header->blocks;
	blocks_this_page = min(blocks, BLOCKS_PER_PAGE - blocki);

	/* This should never happen if there is no previous memory error. */
	if (blocks - blocks_this_page > (alloc->npages - pagei - 1) * BLOCKS_PER_PAGE) {
		fprintf(stderr, "orbit_bitmap_free error: allocation blocks overflow npages size\n");
		return;
	}

	pthread_spin_lock(&alloc->lock);

	for (meta = alloc->page_meta + pagei; blocks;
		++meta, blocks -= blocks_this_page,
		blocks_this_page = min(BLOCKS_PER_PAGE, blocks))
	{
		meta->used -= blocks_this_page;
		if (blocks_this_page == BLOCKS_PER_PAGE)
			bitmap_clear(meta);
		else {
			bitmap_clear_bits(meta, blocki, blocks_this_page);
			blocki = 0;
		}
	}
	for (meta = alloc->page_meta + alloc->allocated - 1; meta >= alloc->page_meta; --meta) {
		if (meta->used != 0)
			break;
		--alloc->allocated;
	}
	if (alloc->data_length)
		*alloc->data_length = alloc->allocated * PAGE_SIZE;

	pthread_spin_unlock(&alloc->lock);
}

void *orbit_bitmap_realloc(struct orbit_allocator *base, void *oldptr, size_t newsize)
{
	struct orbit_bitmap_allocator *alloc = (struct orbit_bitmap_allocator*)base;
	void *mem;
	struct alloc_header *header;
	size_t pagei, blocki;
	size_t newblocks;
	/* struct page_meta *meta; */

	if (!oldptr)
		return orbit_allocator_alloc(base, newsize);

	if (translate(alloc, oldptr, &pagei, &blocki))
		return NULL;

	newblocks = round_up_block(newsize + header_size) / BLOCK_SIZE;

	header = (struct alloc_header*)oldptr - 1;
	if (0 && header->blocks >= newblocks) {
		header->blocks = newblocks;
		/* TODO */

		return oldptr;
	} else if (0) {
		header->blocks = newblocks;
		/* TODO */
		return oldptr;
	}

	mem = orbit_bitmap_alloc(base, newsize);
	memcpy(mem, oldptr, header->blocks * BLOCK_SIZE - header_size);
	obprintf("Memcpying from %p to %p size=%lu\n", oldptr, mem, header->blocks * BLOCK_SIZE - header_size);
	orbit_bitmap_free(base, oldptr);
	return mem;
}

const struct orbit_allocator_vtable orbit_bitmap_allocator_vtable = {
	.alloc = orbit_bitmap_alloc,
	.free = orbit_bitmap_free,
	.realloc = orbit_bitmap_realloc,
	.destroy = orbit_bitmap_allocator_destroy,
};

static const struct orbit_allocator_method orbit_bitmap_default_value = {
	.__create = orbit_bitmap_allocator_create,
};

const struct orbit_allocator_method *orbit_bitmap_default = &orbit_bitmap_default_value;

/* TODO: move this out and write ACU test in the test case file */
int test_bitmap() {
	//struct page_meta meta;
	return 1;
}

/*
 * What xrick's system layer needs that is not part of the game's state
 * (not saved by xr_state.c): the pack, read in place, its open files, the
 * memory block of the allocator, the error handler, the buttons.
 */
#include <stdio.h>
#include <string.h>

#include "xrick/system/system.h"

#include "xr_port.h"

const uint8_t *xr_pack;
uint32_t xr_pack_size;
static uint32_t pack_count;
uint8_t *xr_mem_base;
size_t xr_mem_size;
void (*xr_fatal)(const char *message);
unsigned xr_buttons;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int xr_set_pack(const uint8_t *data, uint32_t size)
{
	if (size < 12 || memcmp(data, "XRK1", 4) || rd32(data + 8) != size) return -1;
	pack_count = rd32(data + 4);
	if (12 + 12 * pack_count > size) return -1;
	xr_pack = data;
	xr_pack_size = size;
	return 0;
}

void xr_set_memory(void *base, size_t size)
{
	xr_mem_base = base;
	xr_mem_size = size;
	xr_memory_reset();
}

/* A file of the pack: data and size, or NULL. */
const uint8_t *xr_pack_file(const char *name, uint32_t *size)
{
	uint32_t lo = 0, hi = pack_count;
	while (lo < hi) {	/* names are sorted */
		uint32_t mid = (lo + hi) / 2;
		const uint8_t *e = xr_pack + 12 + 12 * mid;
		int c = strcmp(name, (const char *)xr_pack + rd32(e));
		if (c == 0) {
			if (size) *size = rd32(e + 8);
			return xr_pack + rd32(e + 4);
		}
		if (c < 0) hi = mid;
		else lo = mid + 1;
	}
	return NULL;
}

/* --- xrick's files: files of the pack --- */

typedef struct {
	const uint8_t *data;
	uint32_t size, pos;
	bool used;
} xr_file_t;

static xr_file_t files[4];

bool sysfile_setRootPath(const char *name)
{
	(void)name;
	return true;
}

void sysfile_clearRootPath(void) {}

file_t sysfile_open(const char *name)
{
	uint32_t size;
	const uint8_t *data = xr_pack_file(name, &size);
	if (!data) return NULL;
	for (unsigned i = 0; i < sizeof files / sizeof files[0]; i++) {
		if (!files[i].used) {
			files[i] = (xr_file_t){ data, size, 0, true };
			return &files[i];
		}
	}
	sys_error("(file) too many open files");
	return NULL;
}

int sysfile_read(file_t file, void *buf, size_t size, size_t count)
{
	xr_file_t *f = file;
	if (!size) return 0;
	size_t n = (f->size - f->pos) / size;
	if (n > count) n = count;
	memcpy(buf, f->data + f->pos, n * size);
	f->pos += n * size;
	return (int)n;
}

int sysfile_seek(file_t file, long offset, int origin)
{
	xr_file_t *f = file;
	long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? (long)f->pos : (long)f->size;
	long pos = base + offset;
	if (pos < 0 || pos > (long)f->size) return -1;
	f->pos = (uint32_t)pos;
	return 0;
}

int sysfile_tell(file_t file) { return (int)((xr_file_t *)file)->pos; }

off_t sysfile_size(file_t file) { return (off_t)((xr_file_t *)file)->size; }

void sysfile_close(file_t file)
{
	if (file) ((xr_file_t *)file)->used = false;
}


/*
 * Save states: the whole game, as memory.
 *
 * Every variable of xrick (src/xrick) and of xr_sys.c is in two sections of
 * their own, xrstate_data and xrstate_bss (the build renames the objects'
 * .data and .bss: Makefile, host/Makefile.host; the device linker script
 * keeps them inside .data / .bss, src/port/xr_core.ld). A state is those
 * two blocks and the used part of xrick's memory block (resources, lists).
 *
 * The pointers in there are only valid for this build (same addresses)
 * with the memory block at the same address: the header checks both. The
 * pack may be elsewhere in flash than when the state was saved: pointers
 * into it (sounds) are moved (xr_sound_rebase).
 */
#include <stdint.h>
#include <string.h>

#include "xr_port.h"
#include "xr_state.h"

extern char __start_xrstate_data[], __stop_xrstate_data[];
extern char __start_xrstate_bss[], __stop_xrstate_bss[];

#define MAGIC 0x31535258	/* "XRS1" */

typedef struct {
	uint32_t magic;
	uint32_t data_size, bss_size;
	uint32_t mem_used, pack_size, pack_crc, reserved;
	uint64_t data_address, mem_address, pack_address;
} header_t;

static uint32_t crc32(const uint8_t *p, size_t n)
{
	uint32_t crc = ~0u;
	while (n--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
	}
	return ~crc;
}

/* The pack's file table: tells which pack a state was made with. */
static uint32_t pack_crc(void)
{
	uint32_t n = xr_pack_size < 4096 ? xr_pack_size : 4096;
	return crc32(xr_pack, n);
}

static void make_header(header_t *h)
{
	memset(h, 0, sizeof *h);
	h->magic = MAGIC;
	h->data_size = (uint32_t)(__stop_xrstate_data - __start_xrstate_data);
	h->bss_size = (uint32_t)(__stop_xrstate_bss - __start_xrstate_bss);
	h->data_address = (uintptr_t)__start_xrstate_data;
	h->mem_address = (uintptr_t)xr_mem_base;
	h->mem_used = (uint32_t)xr_memory_used();
	h->pack_address = (uintptr_t)xr_pack;
	h->pack_size = xr_pack_size;
	h->pack_crc = pack_crc();
}

int xr_state_save(FILE *f)
{
	header_t h;
	make_header(&h);
	if (fwrite(&h, sizeof h, 1, f) != 1) return -1;
	if (h.data_size && fwrite(__start_xrstate_data, h.data_size, 1, f) != 1) return -1;
	if (h.bss_size && fwrite(__start_xrstate_bss, h.bss_size, 1, f) != 1) return -1;
	if (h.mem_used && fwrite(xr_mem_base, h.mem_used, 1, f) != 1) return -1;
	return 0;
}

int xr_state_load(FILE *f)
{
	header_t h, now;
	make_header(&now);
	if (fread(&h, sizeof h, 1, f) != 1) return -1;
	if (h.magic != now.magic || h.data_size != now.data_size || h.bss_size != now.bss_size ||
	    h.data_address != now.data_address || h.mem_address != now.mem_address || h.mem_used > xr_mem_size ||
	    h.pack_size != now.pack_size || h.pack_crc != now.pack_crc)
		return -1;
	/* from here on, a failure leaves the game half loaded */
	if ((h.data_size && fread(__start_xrstate_data, h.data_size, 1, f) != 1) ||
	    (h.bss_size && fread(__start_xrstate_bss, h.bss_size, 1, f) != 1) ||
	    (h.mem_used && fread(xr_mem_base, h.mem_used, 1, f) != 1)) {
		if (xr_fatal) xr_fatal("save state: read error");
		return -1;
	}
	xr_sound_rebase((const uint8_t *)(uintptr_t)h.pack_address, h.pack_size);
	return 0;
}

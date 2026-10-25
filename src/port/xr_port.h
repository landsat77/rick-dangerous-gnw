/* The port layer between xrick (src/xrick) and a front end (console or PC). */
#ifndef XR_PORT_H
#define XR_PORT_H

#include <stddef.h>
#include <stdint.h>

#define XR_WIDTH 320
#define XR_HEIGHT 200
#define XR_SAMPLE_RATE 22050
#define XR_FRAME_MS 40		/* one game frame (25 per second) */

/* The pack (tools/make_pack.py), kept as it is: 0, or -1 if not a pack. */
int xr_set_pack(const uint8_t *data, uint32_t size);
const uint8_t *xr_pack_file(const char *name, uint32_t *size);

extern const uint8_t *xr_pack;
extern uint32_t xr_pack_size;

/* The memory of xrick's allocator (resources, rectangles). */
void xr_set_memory(void *base, size_t size);
void xr_memory_reset(void);
size_t xr_memory_peak(void);
size_t xr_memory_used(void);
extern uint8_t *xr_mem_base;
extern size_t xr_mem_size;

/* Called on an error (resources, memory); may not return. */
extern void (*xr_fatal)(const char *message);

/* The controls (control.h: Control_UP ... Control_FIRE), read by xrick each
   frame. */
extern unsigned xr_buttons;

/* The game's clock: the front end adds XR_FRAME_MS before each frame. */
extern uint32_t xr_clock_ms;

/* The picture: 320 x 200, RGB565. */
extern uint16_t xr_palette[256];
void xr_present(uint16_t *dest, int pitch);

/* n samples at 22050 Hz, mono. */
void xr_sound_mix(int16_t *out, int n);
void xr_sound_rebase(const uint8_t *old_base, uint32_t size);

#endif

/*
 * xrick's system layer (xrick/system/system.h) for the Game & Watch, on
 * nothing but this file's interface (xr_port.h): the same code runs on the
 * console and in the PC build.
 *
 * - Files: the pack (xr_pack.c).
 * - Memory: xrick's stack allocator (sysmem_push/pop) in a block given by
 *   the caller.
 * - Video: the 320 x 200 8-bit frame buffer; xr_present() converts it.
 * - Time: the game's clock, xr_clock_ms (40 ms per game frame).
 * - Sound: 8-bit mono 22050 Hz samples played in place from the pack,
 *   mixed by xr_sound_mix() at 22050 Hz.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "xrick/system/system.h"
#include "xrick/config.h"
#include "xrick/control.h"
#include "xrick/game.h"
#include "xrick/draw.h"
#include "xrick/data/sounds.h"
#include "xrick/system/basic_funcs.h"

#include "xr_port.h"

/* --- state (saved by xr_state.c, like all of xrick's variables) --- */

static uint8_t *mem_top;
static size_t mem_used, mem_peak;
static U8 framebuffer[SYSVID_WIDTH * SYSVID_HEIGHT];
U8 *sysvid_fb = framebuffer;
uint16_t xr_palette[256];
uint32_t xr_clock_ms;

U8 syskbd_up, syskbd_down, syskbd_left, syskbd_right, syskbd_pause, syskbd_end, syskbd_xtra, syskbd_fire;
int sysarg_args_period = 0, sysarg_args_map = 0, sysarg_args_submap = 0;
int sysarg_args_fullscreen = 0, sysarg_args_zoom = 0;
bool sysarg_args_nosound = false;
int sysarg_args_vol = 0;
const char *sysarg_args_data = NULL;
const char *sysfile_defaultPath = "";

void xr_memory_reset(void)
{
	mem_top = xr_mem_base;
	mem_used = mem_peak = 0;
}

size_t xr_memory_peak(void) { return mem_peak; }
size_t xr_memory_used(void) { return mem_used; }

void xr_present(uint16_t *dest, int pitch)
{
	const U8 *s = framebuffer;
	for (int y = 0; y < SYSVID_HEIGHT; y++, dest += pitch)
		for (int x = 0; x < SYSVID_WIDTH; x++) dest[x] = xr_palette[*s++];
}

/* --- main section --- */

bool sys_init(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return true;
}

void sys_shutdown(void) {}

void sys_error(const char *err, ...)
{
	char s[96];
	va_list ap;
	va_start(ap, err);
	vsnprintf(s, sizeof s, err, ap);
	va_end(ap);
	printf("xrick: error: %s\n", s);
	if (xr_fatal) xr_fatal(s);
}

void sys_printf(const char *msg, ...)
{
#ifdef XR_VERBOSE
	va_list ap;
	va_start(ap, msg);
	vprintf(msg, ap);
	va_end(ap);
#else
	(void)msg;
#endif
}

void sys_snprintf(char *buf, size_t size, const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vsnprintf(buf, size, msg, ap);
	va_end(ap);
}

size_t sys_strlen(const char *str) { return strlen(str); }

U32 sys_gettime(void) { return xr_clock_ms; }

void sys_yield(void) {}

bool sys_cacheData(void)
{
	/* the sounds are loaded when first played (syssnd_play) */
	return true;
}

void sys_uncacheData(void) {}

/* --- memory section: a stack --- */

enum { ALIGNMENT = 8 };

bool sysmem_init(void) { return xr_mem_base != NULL; }

void sysmem_shutdown(void) {}

void *sysmem_push(size_t size)
{
	size_t needed = sizeof(size_t) + size + ALIGNMENT;
	if (mem_used + needed > xr_mem_size) {
		sys_error("(memory) full: %u + %u bytes", (unsigned)mem_used, (unsigned)size);
		return NULL;
	}
	uintptr_t aligned = ((uintptr_t)mem_top + sizeof(size_t) + ALIGNMENT - 1) & ~(uintptr_t)(ALIGNMENT - 1);
	((size_t *)aligned)[-1] = needed;
	mem_top += needed;
	mem_used += needed;
	if (mem_used > mem_peak) mem_peak = mem_used;
	return (void *)aligned;
}

void sysmem_pop(void *p)
{
	if (!p) return;
	size_t size = ((size_t *)p)[-1];
	mem_top -= size;
	mem_used -= size;
}

/* --- video section --- */

bool sysvid_init(void) { return true; }
void sysvid_shutdown(void) {}
void sysvid_update(const rect_t *rects) { (void)rects; }
void sysvid_clear(void) { memset(framebuffer, 0, sizeof framebuffer); }
void sysvid_zoom(S8 z) { (void)z; }
void sysvid_toggleFullscreen(void) {}

void sysvid_setPalette(img_color_t *pal, U16 n)
{
	for (U16 i = 0; i < n && i < 256; i++)
		xr_palette[i] = (uint16_t)((pal[i].r >> 3) << 11 | (pal[i].g >> 2) << 5 | pal[i].b >> 3);
}

void sysvid_setGamePalette(void) { sysvid_setPalette(game_colors, game_color_count); }

/* --- events: the buttons, set by the caller (xr_buttons) --- */

void sysevt_poll(void)
{
	static const control_t map[] = { Control_UP, Control_DOWN, Control_LEFT, Control_RIGHT,
	                                 Control_PAUSE, Control_END, Control_EXIT, Control_FIRE };
	for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++) {
		if (xr_buttons & map[i]) control_set(map[i]);
		else control_clear(map[i]);
	}
}

void sysevt_wait(void) { sysevt_poll(); }

/* --- sound section --- */

#ifdef ENABLE_SOUND

const U8 syssnd_period = 20;

enum { CHANNELS = 8 };

typedef struct {
	sound_t *sound;
	const U8 *buf;
	U32 len;
	S8 loop;
} channel_t;

static channel_t channels[CHANNELS];
static bool paused;

bool syssnd_init(void)
{
	memset(channels, 0, sizeof channels);
	paused = false;
	return true;
}

void syssnd_shutdown(void) {}
void syssnd_update(void) {}
void syssnd_vol(S8 v) { (void)v; }
void syssnd_toggleMute(void) {}

/* The samples, in place in the pack (after the 44-byte WAV header the
   resources already checked). */
static void load(sound_t *sound)
{
	uint32_t size;
	const uint8_t *data = xr_pack_file(sound->name, &size);
	if (!data || size < 44) {
		sys_error("(audio) can not load %s", sound->name);
		return;
	}
	sound->buf = (U8 *)data + 44;
	if (sound->len > size - 44) sound->len = size - 44;
}

void syssnd_play(sound_t *sound, S8 loop)
{
	unsigned c = 0;
	if (!sound) return;
	if (!sound->buf) load(sound);
	if (!sound->buf) return;
	/* the same sound restarts on its channel, else a free one */
	while (c < CHANNELS && channels[c].sound != sound && channels[c].loop != 0) c++;
	if (c >= CHANNELS) return;
	channels[c] = (channel_t){ sound, sound->buf, sound->len, loop };
}

void syssnd_pauseAll(bool pause) { paused = pause; }

void syssnd_stop(sound_t *sound)
{
	for (unsigned c = 0; c < CHANNELS; c++)
		if (channels[c].sound == sound) channels[c] = (channel_t){ 0 };
}

void syssnd_stopAll(void) { memset(channels, 0, sizeof channels); }

void xr_sound_mix(int16_t *out, int n)
{
	memset(out, 0, n * sizeof *out);
	if (paused) return;
	for (unsigned c = 0; c < CHANNELS; c++) {
		channel_t *ch = &channels[c];
		int i = 0;
		while (ch->sound && ch->loop != 0 && i < n) {
			int count = n - i;
			if ((U32)count > ch->len) count = (int)ch->len;
			for (int k = 0; k < count; k++) {
				int s = out[i + k] + ((ch->buf[k] - 0x80) << 8);
				out[i + k] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
			}
			ch->buf += count;
			ch->len -= count;
			i += count;
			if (ch->len == 0) {
				if (ch->loop > 0) ch->loop--;
				if (ch->loop) {
					ch->buf = ch->sound->buf;
					ch->len = ch->sound->len;
				} else {
					*ch = (channel_t){ 0 };
				}
			}
		}
	}
}

/* After a state from another session was loaded: the pointers into the
   pack (old_base) move to where the pack is now. */
static void rebase(const U8 **p, const uint8_t *old_base, uint32_t size)
{
	if (*p && *p >= old_base && *p <= old_base + size) *p = xr_pack + (*p - old_base);
}

void xr_sound_rebase(const uint8_t *old_base, uint32_t size)
{
	sound_t *all[] = { soundBombshht, soundBonus, soundBox, soundBullet, soundCrawl, soundDie, soundExplode,
	                   soundGameover, soundJump, soundPad, soundSbonus1, soundSbonus2, soundStick, soundTune0,
	                   soundTune1, soundTune2, soundTune3, soundTune4, soundTune5, soundWalk };
	for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++)
		if (all[i]) rebase((const U8 **)&all[i]->buf, old_base, size);
	for (unsigned i = 0; i < SOUNDS_NBR_ENTITIES; i++)
		if (soundEntity[i]) rebase((const U8 **)&soundEntity[i]->buf, old_base, size);
	for (unsigned c = 0; c < CHANNELS; c++) rebase(&channels[c].buf, old_base, size);
}

#else

void xr_sound_rebase(const uint8_t *old_base, uint32_t size) { (void)old_base; (void)size; }

void xr_sound_mix(int16_t *out, int n) { memset(out, 0, n * sizeof *out); }

#endif /* ENABLE_SOUND */

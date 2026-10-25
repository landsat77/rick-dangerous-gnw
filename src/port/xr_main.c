/*
 * Rick Dangerous (xrick) for Retro-Go SD (Game & Watch), as a GWHB homebrew:
 * /homebrews/RickDangerous.bin with RickDangerous.xrk (the game's data,
 * tools/make_pack.py) beside it.
 *
 * - The pack is cached in external flash and read in place (graphics are
 *   copied into RAM by xrick, sounds are played from flash).
 * - The picture: xrick's 320 x 200, centred in the 320 x 240 LCD.
 * - Timing: the LCD at 50 Hz, one game frame every second VBL (25 per
 *   second, xrick's 40 ms); the audio (22050 Hz, 441 samples per VBL)
 *   paces the loop.
 * - Buttons: D-pad, A = fire (with a direction: shoot, bomb, stick), B =
 *   jump (up), GAME or TIME = pause.
 * - High scores: /data/xrick/highscores.bin.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "gw_lcd.h"
#include "gw_audio.h"
#include "rom_manager.h"
#include "odroid_system.h"
#include "appid.h"
#include "odroid_overlay.h"
#include "odroid_settings.h"
#include "gw_malloc.h"
#include "rg_storage.h"

#ifndef HOST_BUILD
#include "odroid_sdcard.h"
#include "gw_core_bridge.h"
#else
#include "host_compat.h"
#endif

#include "xr_port.h"
#include "xr_state.h"
#include "xrick/system/system.h"
#include "xrick/control.h"
#include "xrick/game.h"
#include "xrick/screens.h"

#define APP_ID APPID_HOMEBREW
#define REFRESH 50
#define SAMPLES_PER_VBL (XR_SAMPLE_RATE / REFRESH)
#define TOP ((HEIGHT - XR_HEIGHT) / 2)
#define MEMORY_SIZE (320 * 1024)	/* xrick's allocator (it needs ~150 KB) */

#ifdef HOST_BUILD
static char save_dir[] = "host_saves/xrick";
#else
static char save_dir[] = RG_BASE_PATH_SAVES "/xrick";
#endif
static char pack_path[256];
static bool picture_ready;

/* --- picture --- */

static void blit(void)
{
	uint16_t *fb = lcd_get_active_buffer();
	memset(fb, 0, WIDTH * TOP * 2);
	xr_present(fb + TOP * WIDTH, WIDTH);
	memset(fb + (TOP + XR_HEIGHT) * WIDTH, 0, WIDTH * (HEIGHT - TOP - XR_HEIGHT) * 2);
	common_ingame_overlay();
}

/* --- high scores --- */

static void scores_path(char *path, size_t size)
{
	snprintf(path, size, "%s/highscores.bin", save_dir);
}

static void scores_load(void)
{
	char path[160];
	scores_path(path, sizeof path);
	FILE *f = fopen(path, "rb");
	if (!f) return;
	hiscore_t table[16];
	size_t n = screen_nbr_hiscores < 16 ? screen_nbr_hiscores : 16;
	if (fread(table, sizeof table[0], n, f) == n) memcpy(screen_highScores, table, n * sizeof table[0]);
	fclose(f);
}

static void scores_save(void)
{
	char path[160];
	scores_path(path, sizeof path);
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fwrite(screen_highScores, sizeof screen_highScores[0], screen_nbr_hiscores, f);
	fclose(f);
}

/* --- system callbacks --- */

static bool LoadState(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	bool ok = xr_state_load(f) == 0;
	fclose(f);
	picture_ready = true;
	return ok;
}

static bool SaveState(const char *path)
{
	FILE *f = fopen(path, "wb");
	if (!f) return false;
	bool ok = xr_state_save(f) == 0;
	fclose(f);
	return ok;
}

static void *Screenshot(void)
{
	lcd_wait_for_vblank();
	blit();
	return lcd_get_active_buffer();
}

static void Shutdown(void) {}

static void SleepWake(void)
{
	odroid_audio_init(XR_SAMPLE_RATE);
	audio_clear_buffers();
	audio_start_playing(SAMPLES_PER_VBL);
}

static void SramSave(void) {}

/* --- errors --- */

static void fatal(const char *message)
{
	printf("xrick: %s\n", message);
#ifdef HOST_BUILD
	exit(1);
#endif
	lcd_clear_buffers();
	odroid_overlay_draw_text(8, 96, WIDTH - 16, "Rick Dangerous:", 0xFFFF, 0x0000);
	odroid_overlay_draw_text(8, 112, WIDTH - 16, message, 0xFFFF, 0x0000);
	odroid_overlay_draw_text(8, 144, WIDTH - 16, "Press a button: back to the menu", 0xFFFF, 0x0000);
	lcd_swap();

	odroid_gamepad_state_t joystick;
	bool released = false;
	while (1) {
		wdog_refresh();
		HAL_Delay(50);
		odroid_input_read_gamepad(&joystick);
		bool any = false;
		for (int i = 0; i < ODROID_INPUT_MAX; i++) any |= joystick.values[i] != 0;
		if (!any) released = true;
		else if (released) odroid_system_switch_app(0);
	}
}

/* --- PC tests (host build): scripted runs --- */

#ifdef HOST_BUILD
#include <stdlib.h>
/*
 * XR_FRAMES=n      quit after n VBLs (50 per second)
 * XR_SCRIPT=list   "vbl:buttons,..." from that VBL on, buttons held (letters
 *                  U D L R A B P = pause, - = none)
 * XR_SHOTS=dir     with XR_SHOT_AT="vbl,vbl,...": the LCD as PPM files
 * XR_WAV=file      the sound (raw 16-bit mono 22050 Hz)
 * XR_FAST=1        no real-time pacing
 * XR_SAVE_AT=vbl / XR_LOAD_AT=vbl (file XR_STATE): save / load a state
 */
static int test_vbl;
static FILE *test_wav;

static bool in_list(const char *list, int value)
{
	for (const char *p = list; p && *p;) {
		if (atoi(p) == value) return true;
		p = strchr(p, ',');
		if (p) p++;
	}
	return false;
}

static void test_input(odroid_gamepad_state_t *j)
{
	const char *script = getenv("XR_SCRIPT");
	const char *held = NULL;
	for (const char *p = script; p && *p;) {
		int at = atoi(p);
		const char *colon = strchr(p, ':');
		if (!colon || at > test_vbl) break;
		held = colon + 1;
		p = strchr(colon, ',');
		if (p) p++;
	}
	if (!held) return;
	for (const char *c = held; *c && *c != ','; c++) {
		switch (*c) {
		case 'U': j->values[ODROID_INPUT_UP] = 1; break;
		case 'D': j->values[ODROID_INPUT_DOWN] = 1; break;
		case 'L': j->values[ODROID_INPUT_LEFT] = 1; break;
		case 'R': j->values[ODROID_INPUT_RIGHT] = 1; break;
		case 'A': j->values[ODROID_INPUT_A] = 1; break;
		case 'B': j->values[ODROID_INPUT_B] = 1; break;
		case 'P': j->values[ODROID_INPUT_X] = 1; break;
		}
	}
}

static void test_shot(void)
{
	const char *dir = getenv("XR_SHOTS");
	if (!dir || !in_list(getenv("XR_SHOT_AT"), test_vbl - 1)) return;
	blit();
	char path[300];
	snprintf(path, sizeof path, "%s/shot_%06d.ppm", dir, test_vbl - 1);
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
	const uint16_t *p = lcd_get_active_buffer();
	for (int i = 0; i < WIDTH * HEIGHT; i++) {
		uint8_t rgb[3] = { (uint8_t)((p[i] >> 11) << 3), (uint8_t)(((p[i] >> 5) & 63) << 2), (uint8_t)((p[i] & 31) << 3) };
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
}

static void test_step(void)
{
	const char *frames = getenv("XR_FRAMES");
	const char *state = getenv("XR_STATE");
	if (state && getenv("XR_SAVE_AT") && atoi(getenv("XR_SAVE_AT")) == test_vbl)
		printf("xrick: save %s\n", SaveState(state) ? "ok" : "FAILED");
	if (state && getenv("XR_LOAD_AT") && atoi(getenv("XR_LOAD_AT")) == test_vbl)
		printf("xrick: load %s\n", LoadState(state) ? "ok" : "FAILED");
	if (frames && test_vbl >= atoi(frames)) {
		if (test_wav) fclose(test_wav);
		exit(0);
	}
	test_vbl++;
}
#endif

/* --- main --- */

void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
	odroid_gamepad_state_t joystick;
	odroid_dialog_choice_t options[1] = { ODROID_DIALOG_CHOICE_LAST };

#ifndef HOST_BUILD
	gw_core_bridge_init();
#endif

	if (start_paused) {
		common_emu_state.pause_after_frames = 2;
		odroid_audio_mute(true);
	} else {
		common_emu_state.pause_after_frames = 0;
	}
	common_emu_state.frame_time_10us = 100000 / REFRESH;
	lcd_set_refresh_rate(REFRESH);

	odroid_system_init(APP_ID, XR_SAMPLE_RATE);
	odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, &Shutdown, &SleepWake, &SramSave, NULL);

	lcd_clear_buffers();
	xr_fatal = fatal;

	/* The pack beside the .bin: ".../RickDangerous.bin" -> ".../RickDangerous.xrk" */
	if (!ACTIVE_FILE || !ACTIVE_FILE->path[0]) fatal("no path");
	snprintf(pack_path, sizeof pack_path, "%s", ACTIVE_FILE->path);
	char *extension = strrchr(pack_path, '.');
	if (extension) *extension = 0;
	strncat(pack_path, ".xrk", sizeof pack_path - strlen(pack_path) - 1);
	FILE *f = fopen(pack_path, "rb");
	if (!f) fatal("RickDangerous.xrk not found beside the .bin");
	fseek(f, 0, SEEK_END);
	uint32_t pack_size = ftell(f);
	fclose(f);
	const uint8_t *pack = odroid_overlay_cache_file_in_flash(pack_path, &pack_size, false);
	if (!pack || !pack_size) fatal("cannot map the pack in flash");
	if (xr_set_pack(pack, pack_size)) fatal("not a Rick Dangerous pack (.xrk)");

	void *memory = ram_malloc(MEMORY_SIZE);
	if (!memory) fatal("not enough RAM");
	xr_set_memory(memory, MEMORY_SIZE);

	odroid_sdcard_mkdir(save_dir);

	sysmem_init();
	syssnd_init();
	if (!game_gnw_init()) fatal("cannot load the game data");
	scores_load();
	printf("xrick: pack %lu bytes at %p, memory %lu KB used\n", (unsigned long)pack_size, (const void *)pack,
	       (unsigned long)(xr_memory_peak() / 1024));

	audio_start_playing(SAMPLES_PER_VBL);
#ifdef HOST_BUILD
	if (getenv("XR_FAST")) odroid_audio_mute(true);
#endif
	if (load_state) odroid_system_emu_load_state(save_slot);

	int vbl = 0;
	while (1) {
		wdog_refresh();

		bool draw_frame = common_emu_frame_loop();

		odroid_input_read_gamepad(&joystick);
		common_emu_input_loop(&joystick, options, &blit);
#ifdef HOST_BUILD
		test_input(&joystick);
		test_step();
#endif

		unsigned b = 0;
		if (joystick.values[ODROID_INPUT_UP] || joystick.values[ODROID_INPUT_B]) b |= Control_UP;
		if (joystick.values[ODROID_INPUT_DOWN]) b |= Control_DOWN;
		if (joystick.values[ODROID_INPUT_LEFT]) b |= Control_LEFT;
		if (joystick.values[ODROID_INPUT_RIGHT]) b |= Control_RIGHT;
		if (joystick.values[ODROID_INPUT_A]) b |= Control_FIRE;
		if (joystick.values[ODROID_INPUT_X] || joystick.values[ODROID_INPUT_Y] ||
		    joystick.values[ODROID_INPUT_START] || joystick.values[ODROID_INPUT_SELECT])
			b |= Control_PAUSE;
		xr_buttons = b;

		/* a game frame every second VBL */
		if (++vbl >= 2) {
			vbl = 0;
			bool was_getname = game_gnw_getname();
			xr_clock_ms += XR_FRAME_MS;
			if (!game_gnw_frame()) odroid_system_switch_app(0);
			if (was_getname && !game_gnw_getname()) scores_save();
			picture_ready = true;
		}

#ifdef HOST_BUILD
		test_shot();
#endif
		if (draw_frame && picture_ready) {
			blit();
			lcd_swap();
			picture_ready = false;
		}

		if (!common_emu_sound_loop_is_muted()) {
			int16_t *buffer = audio_get_active_buffer();
			uint16_t length = audio_get_buffer_length();
			if (buffer && length) {
				xr_sound_mix(buffer, length);
				int volume = common_emu_sound_get_volume();
				for (uint16_t i = 0; i < length; i++) buffer[i] = buffer[i] * volume / 255;
#ifdef HOST_BUILD
				if (!test_wav && getenv("XR_WAV")) test_wav = fopen(getenv("XR_WAV"), "wb");
				if (test_wav) fwrite(buffer, 2, length, test_wav);
#endif
			}
		}
		common_emu_sound_sync(false);
	}
}

# Changelog

This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Release tags must
match a section heading exactly (for example `v1.0.0`): CI uses that section
as the GitHub Release notes.

## [Unreleased]

## [v1.0.0] - 2026-10-25

### Added

- Rick Dangerous (xrick, Pierluigi Vicinanza's 2014 version) as a GWHB
  homebrew for Retro-Go SD (firmware ABI v2): `/homebrews/RickDangerous.bin`
  with its pack `RickDangerous.xrk` (the game's data, 3.4 MB, cached in the
  external flash and read in place).
- 25 frames per second on a 50 Hz LCD, sound and music at 22050 Hz.
- Buttons: D-pad, A = fire (shoot / bomb / stick with a direction), B =
  jump, GAME or TIME = pause.
- Save states (Retro-Go menu, resume from the launcher) and high scores kept
  on the SD card.
- PC version (`make host`) with scripted runs for tests.

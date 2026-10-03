// SPDX-License-Identifier: MIT
//
// What differs between desktops.
//
// THE SHAPE. On macOS the window is borderless, not opaque, with a clear
// background and an OpenGL surface whose opacity is 0: what the player
// paints with alpha 0 is not there, the window server passes clicks through
// it, and the system shadow follows the painted outline (mac_window.mm).
// Elsewhere SDL's shaped-window API cuts the window to a mask taken from the
// first painted frame (X11 SHAPE, Win32 regions) — a hard edge, since those
// have no per-pixel window alpha without a compositor.
//
// THE PICKERS. A system file / folder dialog, asked for the way a shell
// script asks: osascript on macOS, zenity or kdialog on Linux. Nothing is
// linked for it; where none answers, the picker returns nothing.

#pragma once
#include <SDL.h>

#include <string>
#include <vector>

void platformMakeTransparent(SDL_Window* win);  // macOS; no-op elsewhere
void platformRefreshShadow(SDL_Window* win);    // macOS; no-op elsewhere
bool platformUsesShapeApi();                    // false on macOS
// Let mouse events through the window to whatever is under it (true) or
// take them (false). macOS only: a layer-backed OpenGL window is hit-tested
// as a rectangle whatever its alpha, so the host switches this as the
// pointer crosses the skin's outline. Elsewhere the window's shape already
// does it, and this is a no-op.
void platformSetClickThrough(SDL_Window* win, bool through);

std::vector<std::string> pickAudioFiles();
std::string pickFolder();
// Every .mp3 / .wav under a folder, sorted by path.
std::vector<std::string> audioFilesUnder(const std::string& dir);
bool isAudioFile(const std::string& path);

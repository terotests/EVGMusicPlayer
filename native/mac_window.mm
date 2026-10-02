// SPDX-License-Identifier: AGPL-3.0-or-later
//
// macOS: a borderless SDL window made see-through. The window is not opaque,
// its background is clear, and the OpenGL surface's opacity is 0, so the
// alpha the player paints is the window's alpha: the transparent parts are
// not drawn, clicks there go to whatever is underneath, and the shadow the
// window server draws follows the painted outline.

#import <Cocoa/Cocoa.h>
#include <SDL.h>
#include <SDL_syswm.h>

#include "platform.h"

static NSWindow* nsWindow(SDL_Window* win) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_COCOA) return nil;
  return info.info.cocoa.window;
}

void platformMakeTransparent(SDL_Window* win) {
  NSWindow* w = nsWindow(win);
  if (!w) return;
  [w setOpaque:NO];
  [w setBackgroundColor:[NSColor clearColor]];
  [w setHasShadow:YES];
  NSOpenGLContext* ctx = [NSOpenGLContext currentContext];
  if (ctx) {
    GLint opacity = 0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [ctx setValues:&opacity forParameter:NSOpenGLContextParameterSurfaceOpacity];
#pragma clang diagnostic pop
  }
}

// The shadow is computed from what is on screen when it is asked for; after
// the first frame is up it is asked again, or it is the rectangle's.
void platformRefreshShadow(SDL_Window* win) {
  NSWindow* w = nsWindow(win);
  if (w) [w invalidateShadow];
}

bool platformUsesShapeApi() { return false; }

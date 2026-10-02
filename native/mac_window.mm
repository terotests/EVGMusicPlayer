// SPDX-License-Identifier: MIT
//
// macOS: a borderless SDL window made see-through, so the alpha the player
// paints is the window's alpha: the transparent parts are not drawn, clicks
// there go to whatever is underneath, and the window server's shadow follows
// the painted outline.
//
// Three things have to be non-opaque, and any one left opaque shows as black
// where the skin paints nothing:
//
//   the NSWindow        setOpaque:NO, a clear background colour
//   the OpenGL surface  NSOpenGLContextParameterSurfaceOpacity = 0
//   the view's layers   on macOS 10.14 and later every view is layer-backed,
//                       and the OpenGL content is composited through the
//                       content view's layer; that layer (and the one the
//                       context draws into) must be marked not opaque, or the
//                       surface opacity above is ignored
//
// SDL creates some of this lazily and may set it again as the window is
// shown, so `platformRefreshShadow` applies all of it once more after the
// first frames are on screen. EVGP_DEBUG=1 prints what each one ended up as.

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <SDL.h>
#include <SDL_syswm.h>

#include <cstdio>
#include <cstdlib>

#include "platform.h"

static NSWindow* nsWindow(SDL_Window* win) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_COCOA) return nil;
  return info.info.cocoa.window;
}

static void clearLayer(CALayer* layer) {
  if (!layer) return;
  layer.opaque = NO;
  layer.backgroundColor = CGColorGetConstantColor(kCGColorClear);
  for (CALayer* sub in layer.sublayers) clearLayer(sub);
}

static void apply(SDL_Window* win, const char* when) {
  NSWindow* w = nsWindow(win);
  if (!w) return;
  [w setOpaque:NO];
  [w setBackgroundColor:[NSColor clearColor]];
  [w setHasShadow:YES];

  NSView* view = [w contentView];
  clearLayer(view.layer);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  NSOpenGLContext* ctx = [NSOpenGLContext currentContext];
  GLint opacity = -1;
  if (ctx) {
    GLint zero = 0;
    [ctx setValues:&zero forParameter:NSOpenGLContextParameterSurfaceOpacity];
    [ctx update];
    [ctx getValues:&opacity forParameter:NSOpenGLContextParameterSurfaceOpacity];
    if (ctx.view && ctx.view != view) clearLayer(ctx.view.layer);
  }
#pragma clang diagnostic pop

  if (std::getenv("EVGP_DEBUG")) {
    std::fprintf(stderr,
                 "mac (%s): window opaque=%d, view layer=%s opaque=%d sublayers=%lu, gl context=%s surface opacity=%d\n",
                 when, (int)[w isOpaque], view.layer ? "yes" : "no", view.layer ? (int)view.layer.opaque : -1,
                 view.layer ? (unsigned long)view.layer.sublayers.count : 0UL, ctx ? "yes" : "no", (int)opacity);
  }
}

void platformMakeTransparent(SDL_Window* win) { apply(win, "created"); }

// After the first frames: everything once more, then the shadow, which is
// computed from what is on screen when it is asked for.
void platformRefreshShadow(SDL_Window* win) {
  apply(win, "first frames");
  NSWindow* w = nsWindow(win);
  if (w) [w invalidateShadow];
}

bool platformUsesShapeApi() { return false; }

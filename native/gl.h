// SPDX-License-Identifier: AGPL-3.0-or-later
// OpenGL 3.3 core declarations, per platform. Windows would need a loader
// (glad / GLEW); it is not wired up.
#pragma once
#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

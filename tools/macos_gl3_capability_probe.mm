// SPDX-License-Identifier: GPL-3.0-or-later
//
// Probe the exact accelerated OpenGL context class required by OGRE GL3Plus
// before launching a packaged application on a CI host.  Exit code 78 means
// the host lacks this optional runtime capability; every other non-zero exit
// is an unexpected probe failure.

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <OpenGL/gl3.h>

#include <cstdio>

namespace
{

constexpr int CAPABILITY_UNAVAILABLE = 78;
constexpr int PROBE_FAILURE = 70;

int ReportUnavailable(const char* reason)
{
    std::printf(
        "ROR_MACOS_GL3_CAPABILITY=unavailable reason=%s\n",
        reason);
    return CAPABILITY_UNAVAILABLE;
}

int ReportFailure(const char* reason)
{
    std::fprintf(
        stderr,
        "ROR_MACOS_GL3_CAPABILITY=error reason=%s\n",
        reason);
    return PROBE_FAILURE;
}

} // namespace

int main()
{
    @autoreleasepool
    {
        @try
        {
            // Keep this list aligned with OgreOSXCocoaWindow.mm.  In
            // particular, GL3Plus requires an accelerated 3.2 Core context
            // and deliberately disallows falling back to a software renderer.
            NSOpenGLPixelFormatAttribute attributes[] = {
                NSOpenGLPFAScreenMask,
                static_cast<NSOpenGLPixelFormatAttribute>(
                    CGDisplayIDToOpenGLDisplayMask(CGMainDisplayID())),
                NSOpenGLPFAOpenGLProfile,
                NSOpenGLProfileVersion3_2Core,
                NSOpenGLPFANoRecovery,
                NSOpenGLPFAAccelerated,
                NSOpenGLPFADoubleBuffer,
                NSOpenGLPFAColorSize,
                static_cast<NSOpenGLPixelFormatAttribute>(32),
                NSOpenGLPFAAlphaSize,
                static_cast<NSOpenGLPixelFormatAttribute>(8),
                NSOpenGLPFAStencilSize,
                static_cast<NSOpenGLPixelFormatAttribute>(8),
                NSOpenGLPFADepthSize,
                static_cast<NSOpenGLPixelFormatAttribute>(16),
                static_cast<NSOpenGLPixelFormatAttribute>(0),
            };

            NSOpenGLPixelFormat* pixel_format =
                [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
            if (pixel_format == nil)
            {
                return ReportUnavailable(
                    "accelerated-core-pixel-format-unavailable");
            }

            NSOpenGLContext* context =
                [[NSOpenGLContext alloc] initWithFormat:pixel_format
                                          shareContext:nil];
            if (context == nil)
            {
                return ReportUnavailable(
                    "accelerated-core-context-unavailable");
            }

            [context makeCurrentContext];
            if ([NSOpenGLContext currentContext] != context)
            {
                return ReportUnavailable("accelerated-core-context-not-current");
            }

            const GLubyte* version = glGetString(GL_VERSION);
            const GLubyte* renderer = glGetString(GL_RENDERER);
            GLint major = 0;
            GLint minor = 0;
            glGetIntegerv(GL_MAJOR_VERSION, &major);
            glGetIntegerv(GL_MINOR_VERSION, &minor);
            if (version == nullptr || renderer == nullptr || major < 3)
            {
                [NSOpenGLContext clearCurrentContext];
                return ReportUnavailable("opengl-3-unavailable");
            }

            std::printf(
                "ROR_MACOS_GL3_CAPABILITY=available version=%d.%d renderer=%s\n",
                major,
                minor,
                reinterpret_cast<const char*>(renderer));
            [NSOpenGLContext clearCurrentContext];
            return 0;
        }
        @catch (NSException* exception)
        {
            std::fprintf(
                stderr,
                "macOS GL3 capability probe raised %s: %s\n",
                exception.name.UTF8String,
                exception.reason.UTF8String);
            return ReportFailure("objective-c-exception");
        }
    }
}

/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#import <AppKit/AppKit.h>

#import "OgreMetalView.h"

#include <cstdint>

namespace RoR {

bool RendererOgreNextCocoaIsMainThread() noexcept {
  return [NSThread isMainThread] == YES;
}

bool RendererOgreNextCocoaCreateMetalView(
    std::uintptr_t cocoa_window, void **metal_view) noexcept {
  if (cocoa_window == 0U || metal_view == nullptr) {
    return false;
  }
  *metal_view = nullptr;
  NSWindow *window = (__bridge NSWindow *)(
      reinterpret_cast<void *>(cocoa_window));
  if (![window isKindOfClass:[NSWindow class]] || window.contentView == nil) {
    return false;
  }

  NSView *content_view = window.contentView;
  OgreMetalView *view =
      [[OgreMetalView alloc] initWithFrame:content_view.bounds];
  if (view == nil) {
    return false;
  }
  view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [content_view addSubview:view];
  if (view.superview != content_view) {
    [view removeFromSuperview];
    return false;
  }
  *metal_view = (__bridge_retained void *)view;
  return true;
}

bool RendererOgreNextCocoaDestroyMetalView(void *metal_view) noexcept {
  if (metal_view == nullptr) {
    return false;
  }
  // Do not consume the retained bridge until AppKit confirms detachment.
  // Returning false means the caller still owns this exact retained pointer
  // and may safely retry destruction.
  OgreMetalView *view = (__bridge OgreMetalView *)metal_view;
  [view removeFromSuperview];
  if (view.superview != nil) {
    return false;
  }
  OgreMetalView *released_view =
      (__bridge_transfer OgreMetalView *)metal_view;
  (void)released_view;
  return true;
}

} // namespace RoR

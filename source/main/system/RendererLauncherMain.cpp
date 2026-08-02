/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererPublicLauncher.h"

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstdio>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argument_count = 0;
  LPWSTR *arguments =
      ::CommandLineToArgvW(::GetCommandLineW(), &argument_count);
  if (arguments == nullptr || argument_count < 1) {
    (void)std::fprintf(
        stderr,
        "RoR renderer launcher: failed-windows-command-line-decode\n");
    (void)std::fflush(stderr);
    return RoR::kRendererPublicLauncherInternalExitCode;
  }
  const int result = RoR::RunRendererPublicLauncher(
      argument_count,
      const_cast<const wchar_t *const *>(arguments));
  (void)::LocalFree(arguments);
  return result;
}

#else

int main(int argc, char *argv[]) {
  return RoR::RunRendererPublicLauncher(
      argc, const_cast<const char *const *>(argv));
}

#endif

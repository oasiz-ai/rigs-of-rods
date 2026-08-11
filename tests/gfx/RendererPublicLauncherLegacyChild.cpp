/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

// Exact test sibling for the real public renderer entrypoint. A no-flag
// launch must replace/spawn this process with no forwarded arguments.

#include <cstdio>

namespace {

template <typename Character>
int VerifyNoForwardedArguments(int argc, Character *argv[]) {
  if (argc != 1) {
    (void)std::fprintf(
        stderr,
        "RoR-Ogre14 launcher test child: expected argc=1, got argc=%d\n",
        argc);
    (void)std::fflush(stderr);
    return 81;
  }
  if (argv == nullptr || argv[0] == nullptr || argv[0][0] == Character()) {
    (void)std::fprintf(
        stderr,
        "RoR-Ogre14 launcher test child: invalid executable argument\n");
    (void)std::fflush(stderr);
    return 82;
  }
  return 0;
}

} // namespace

#if defined(_WIN32)

int wmain(int argc, wchar_t *argv[]) {
  return VerifyNoForwardedArguments(argc, argv);
}

#else

int main(int argc, char *argv[]) {
  return VerifyNoForwardedArguments(argc, argv);
}

#endif

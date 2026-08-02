/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// Test-only renderer child. This target is never installed or packaged.

#include <cstdlib>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>

namespace {

[[noreturn]] void Fail(unsigned int code, const char *message) {
  std::cerr << "renderer-child-fake-error:" << message << '\n';
  ::ExitProcess(code);
}

} // namespace

int wmain(int argc, wchar_t *argv[]) {
  if (argc == 2 &&
      std::wstring(argv[1]) == L"--invoke-launcher-null-stdio") {
    if (::GetStdHandle(STD_INPUT_HANDLE) != nullptr ||
        ::GetStdHandle(STD_OUTPUT_HANDLE) != nullptr ||
        ::GetStdHandle(STD_ERROR_HANDLE) != nullptr) {
      ::ExitProcess(86U);
    }
    ::ExitProcess(0xc0de0043U);
  }
  if (argc == 2 &&
      std::wstring(argv[1]) == L"--invoke-launcher-invalid-stdio") {
    if (::GetStdHandle(STD_INPUT_HANDLE) != INVALID_HANDLE_VALUE ||
        ::GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE ||
        ::GetStdHandle(STD_ERROR_HANDLE) != INVALID_HANDLE_VALUE) {
      ::ExitProcess(87U);
    }
    ::ExitProcess(0xc0de0044U);
  }
  int first = 1;
  if (argc > first &&
      std::wstring(argv[first]) == L"--invoke-launcher") {
    ++first;
  }
  const wchar_t *expected_arguments[] = {
      L"--renderer-child-e2e", L"", L"space value", L"quote\"value",
      L"trailing\\", L"unicode-\u03a9"};
  const int expected_count = static_cast<int>(
      sizeof(expected_arguments) / sizeof(expected_arguments[0]));
  if (argc - first != expected_count) {
    Fail(81U, "argument-count");
  }
  for (int index = 0; index < expected_count; ++index) {
    if (std::wstring(argv[first + index]) != expected_arguments[index]) {
      Fail(82U, "argument-value");
    }
  }

  const wchar_t *token = ::_wgetenv(L"ROR_RENDERER_CHILD_TEST_TOKEN");
  if (token == nullptr || std::wstring(token) != L"renderer-child-env-ok") {
    Fail(83U, "environment");
  }
  const wchar_t *expected_cwd =
      ::_wgetenv(L"ROR_RENDERER_CHILD_EXPECTED_CWD");
  wchar_t *cwd = ::_wgetcwd(nullptr, 0);
  if (expected_cwd == nullptr || cwd == nullptr ||
      std::wstring(cwd) != expected_cwd) {
    std::free(cwd);
    Fail(84U, "working-directory");
  }
  std::free(cwd);

  std::string input;
  std::getline(std::cin, input);
  if (input != "renderer-child-stdin") {
    Fail(85U, "standard-input");
  }
  std::cout << "renderer-child-stdout-ok\n" << std::flush;
  std::cerr << "renderer-child-stderr-ok\n" << std::flush;
  ::ExitProcess(0xc0de0042U);
}

#else

#include <unistd.h>

namespace {

int Fail(int code, const char *message) {
  std::cerr << "renderer-child-fake-error:" << message << '\n';
  return code;
}

} // namespace

int main(int argc, char *argv[]) {
  const char *expected_arguments[] = {
      "--renderer-child-e2e", "", "space value", "quote\"value",
      "trailing\\", "unicode-\xcf\xa9"};
  const int expected_count = static_cast<int>(
      sizeof(expected_arguments) / sizeof(expected_arguments[0]));
  if (argc - 1 != expected_count) {
    return Fail(81, "argument-count");
  }
  for (int index = 0; index < expected_count; ++index) {
    if (std::string(argv[index + 1]) != expected_arguments[index]) {
      return Fail(82, "argument-value");
    }
  }

  const char *token = std::getenv("ROR_RENDERER_CHILD_TEST_TOKEN");
  if (token == nullptr || std::string(token) != "renderer-child-env-ok") {
    return Fail(83, "environment");
  }
  const char *expected_cwd =
      std::getenv("ROR_RENDERER_CHILD_EXPECTED_CWD");
  char *cwd = ::getcwd(nullptr, 0U);
  if (expected_cwd == nullptr || cwd == nullptr ||
      std::string(cwd) != expected_cwd) {
    std::free(cwd);
    return Fail(84, "working-directory");
  }
  std::free(cwd);

  std::string input;
  std::getline(std::cin, input);
  if (input != "renderer-child-stdin") {
    return Fail(85, "standard-input");
  }
  std::cout << "renderer-child-stdout-ok\n" << std::flush;
  std::cerr << "renderer-child-stderr-ok\n" << std::flush;
  return 37;
}

#endif

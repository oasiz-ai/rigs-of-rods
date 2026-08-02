/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererSiblingPath.h"

#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace RoR {
namespace {

bool HasSafeChildBasename(const char *basename) noexcept {
  if (basename == nullptr || basename[0] == '\0') {
    return false;
  }
  constexpr std::size_t maximum_component_length = 255U;
  std::size_t length = 0U;
  for (const char *cursor = basename; *cursor != '\0'; ++cursor) {
    const unsigned char value = static_cast<unsigned char>(*cursor);
    const bool is_ascii_letter =
        (value >= static_cast<unsigned char>('A') &&
         value <= static_cast<unsigned char>('Z')) ||
        (value >= static_cast<unsigned char>('a') &&
         value <= static_cast<unsigned char>('z'));
    const bool is_ascii_digit =
        value >= static_cast<unsigned char>('0') &&
        value <= static_cast<unsigned char>('9');
    if (!(is_ascii_letter || is_ascii_digit || *cursor == '-' ||
          *cursor == '_' || *cursor == '.') ||
        ++length > maximum_component_length) {
      return false;
    }
  }
  // A leading or trailing dot has platform-dependent path semantics. The
  // production siblings never need either spelling, so reject both rather
  // than relying on POSIX/Win32 normalization differences.
  return basename[0] != '.' && basename[length - 1U] != '.';
}

#if defined(_WIN32)

class WindowsHandle final {
public:
  explicit WindowsHandle(HANDLE handle) : handle_(handle) {}
  ~WindowsHandle() {
    if (valid()) {
      (void)::CloseHandle(handle_);
    }
  }
  WindowsHandle(const WindowsHandle &) = delete;
  WindowsHandle &operator=(const WindowsHandle &) = delete;

  [[nodiscard]] HANDLE get() const noexcept { return handle_; }
  [[nodiscard]] bool valid() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }

private:
  HANDLE handle_ = nullptr;
};

bool IsSupportedFinalDosExecutablePath(const std::wstring &path) noexcept {
  constexpr wchar_t extended_prefix[] = L"\\\\?\\";
  if (path.compare(0U, 4U, extended_prefix) != 0) {
    return false;
  }
  constexpr wchar_t unc_prefix[] = L"UNC\\";
  if (path.compare(4U, 4U, unc_prefix) == 0) {
    const std::wstring::size_type server_end = path.find(L'\\', 8U);
    if (server_end == std::wstring::npos || server_end == 8U) {
      return false;
    }
    const std::wstring::size_type share_end =
        path.find(L'\\', server_end + 1U);
    return share_end != std::wstring::npos &&
           share_end != server_end + 1U && share_end + 1U < path.size();
  }
  if (path.size() <= 7U) {
    return false;
  }
  const wchar_t drive = path[4U];
  const bool is_ascii_drive =
      (drive >= L'A' && drive <= L'Z') ||
      (drive >= L'a' && drive <= L'z');
  return is_ascii_drive && path[5U] == L':' && path[6U] == L'\\';
}

bool CurrentExecutablePath(std::wstring &path,
                           std::uint32_t &error_code) {
  std::vector<wchar_t> buffer(512U);
  constexpr std::size_t maximum_windows_path = 32768U;
  for (;;) {
    ::SetLastError(ERROR_SUCCESS);
    const DWORD length = ::GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U) {
      error_code = static_cast<std::uint32_t>(::GetLastError());
      return false;
    }
    if (length < buffer.size()) {
      const std::wstring loaded_path(buffer.data(),
                                     static_cast<std::size_t>(length));
      WindowsHandle executable(::CreateFileW(
          loaded_path.c_str(), FILE_READ_ATTRIBUTES,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
      if (!executable.valid()) {
        error_code = static_cast<std::uint32_t>(::GetLastError());
        return false;
      }
      constexpr DWORD final_flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
      const DWORD required = ::GetFinalPathNameByHandleW(
          executable.get(), nullptr, 0U, final_flags);
      if (required == 0U) {
        error_code = static_cast<std::uint32_t>(::GetLastError());
        return false;
      }
      std::vector<wchar_t> final_path(static_cast<std::size_t>(required));
      const DWORD final_length = ::GetFinalPathNameByHandleW(
          executable.get(), final_path.data(),
          static_cast<DWORD>(final_path.size()), final_flags);
      if (final_length == 0U || final_length >= final_path.size()) {
        error_code = final_length >= final_path.size()
                         ? static_cast<std::uint32_t>(
                               ERROR_INSUFFICIENT_BUFFER)
                         : static_cast<std::uint32_t>(::GetLastError());
        return false;
      }
      path.assign(final_path.data(), static_cast<std::size_t>(final_length));
      if (!IsSupportedFinalDosExecutablePath(path)) {
        path.clear();
        error_code = static_cast<std::uint32_t>(ERROR_INVALID_NAME);
        return false;
      }
      return true;
    }
    if (buffer.size() >= maximum_windows_path) {
      error_code = static_cast<std::uint32_t>(ERROR_INSUFFICIENT_BUFFER);
      return false;
    }
    const std::size_t next_size =
        buffer.size() > maximum_windows_path / 2U
            ? maximum_windows_path
            : buffer.size() * 2U;
    buffer.resize(next_size);
  }
}

bool BuildChildPath(const std::wstring &executable_path,
                    const char *basename, std::wstring &child_path) {
  const std::wstring::size_type separator =
      executable_path.find_last_of(L"/\\");
  if (separator == std::wstring::npos ||
      separator + 1U >= executable_path.size()) {
    return false;
  }
  child_path.assign(executable_path, 0U, separator + 1U);
  for (const char *cursor = basename; *cursor != '\0'; ++cursor) {
    child_path.push_back(static_cast<wchar_t>(
        static_cast<unsigned char>(*cursor)));
  }
  return true;
}

#else

bool CurrentExecutablePath(std::string &path,
                           std::uint32_t &error_code) {
#if defined(__APPLE__)
  std::uint32_t required_size = 0U;
  if (::_NSGetExecutablePath(nullptr, &required_size) != -1 ||
      required_size == 0U) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  std::vector<char> buffer(required_size);
  if (::_NSGetExecutablePath(buffer.data(), &required_size) != 0) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  char *canonical = ::realpath(buffer.data(), nullptr);
  if (canonical == nullptr) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  path.assign(canonical);
  std::free(canonical);
  return !path.empty();
#else
  std::vector<char> buffer(1024U);
  constexpr std::size_t maximum_path = 1024U * 1024U;
  for (;;) {
    const ssize_t length =
        ::readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length < 0) {
      error_code = static_cast<std::uint32_t>(errno);
      return false;
    }
    if (static_cast<std::size_t>(length) < buffer.size()) {
      path.assign(buffer.data(), static_cast<std::size_t>(length));
      return !path.empty();
    }
    if (buffer.size() >= maximum_path) {
      error_code = static_cast<std::uint32_t>(ENAMETOOLONG);
      return false;
    }
    buffer.resize(buffer.size() * 2U);
  }
#endif
}

bool BuildChildPath(const std::string &executable_path,
                    const char *basename, std::string &child_path) {
  const std::string::size_type separator = executable_path.find_last_of('/');
  if (separator == std::string::npos ||
      separator + 1U >= executable_path.size()) {
    return false;
  }
  child_path.assign(executable_path, 0U, separator + 1U);
  child_path.append(basename);
  return true;
}

#endif

} // namespace

RendererSiblingPathResult ResolveRendererSiblingPath(
    const char *basename) noexcept {
  RendererSiblingPathResult result;
  try {
    if (!HasSafeChildBasename(basename)) {
      return result;
    }
    const RendererCurrentExecutablePathResult executable =
        ResolveRendererCurrentExecutablePath();
    result.native_error_code = executable.native_error_code;
    if (!executable.accepted) {
      result.status =
          RendererSiblingPathStatus::FAILED_CURRENT_EXECUTABLE_PATH;
      return result;
    }
    result = ResolveRendererSiblingPathFromExecutable(executable.path,
                                                      basename);
    result.native_error_code = executable.native_error_code;
    return result;
  } catch (...) {
    result.path.clear();
    result.accepted = false;
    result.status = RendererSiblingPathStatus::FAILED_INTERNAL;
    return result;
  }
}

RendererSiblingPathResult ResolveRendererSiblingPathFromExecutable(
    const RendererChildLauncherString &canonical_executable_path,
    const char *basename) noexcept {
  RendererSiblingPathResult result;
  try {
    if (!HasSafeChildBasename(basename)) {
      return result;
    }
    if (!BuildChildPath(canonical_executable_path, basename, result.path)) {
      result.path.clear();
      result.status = RendererSiblingPathStatus::FAILED_CHILD_PATH;
      return result;
    }
    result.status = RendererSiblingPathStatus::READY;
    result.accepted = true;
    return result;
  } catch (...) {
    result.path.clear();
    result.accepted = false;
    result.status = RendererSiblingPathStatus::FAILED_INTERNAL;
    return result;
  }
}

RendererCurrentExecutablePathResult
ResolveRendererCurrentExecutablePath() noexcept {
  RendererCurrentExecutablePathResult result;
  try {
    if (!CurrentExecutablePath(result.path, result.native_error_code) ||
        result.path.empty()) {
      result.path.clear();
      result.status =
          RendererSiblingPathStatus::FAILED_CURRENT_EXECUTABLE_PATH;
      return result;
    }
    result.status = RendererSiblingPathStatus::READY;
    result.accepted = true;
    return result;
  } catch (...) {
    result.path.clear();
    result.accepted = false;
    result.status = RendererSiblingPathStatus::FAILED_INTERNAL;
    return result;
  }
}

bool IsKnownRendererSiblingPathStatus(
    RendererSiblingPathStatus status) noexcept {
  switch (status) {
  case RendererSiblingPathStatus::READY:
  case RendererSiblingPathStatus::REJECTED_INVALID_BASENAME:
  case RendererSiblingPathStatus::FAILED_CURRENT_EXECUTABLE_PATH:
  case RendererSiblingPathStatus::FAILED_CHILD_PATH:
  case RendererSiblingPathStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererSiblingPathStatus status) noexcept {
  switch (status) {
  case RendererSiblingPathStatus::READY:
    return "ready";
  case RendererSiblingPathStatus::REJECTED_INVALID_BASENAME:
    return "rejected-invalid-basename";
  case RendererSiblingPathStatus::FAILED_CURRENT_EXECUTABLE_PATH:
    return "failed-current-executable-path";
  case RendererSiblingPathStatus::FAILED_CHILD_PATH:
    return "failed-child-path";
  case RendererSiblingPathStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR

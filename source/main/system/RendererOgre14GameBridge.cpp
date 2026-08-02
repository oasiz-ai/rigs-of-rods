/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgre14GameBridge.h"

#include <cstddef>
#include <limits>
#include <utility>

namespace RoR {
namespace {

bool HasValidArguments(int argc, char *const argv[]) noexcept {
  if (argc < 1 || argv == nullptr || argv[0] == nullptr ||
      argv[0][0] == '\0') {
    return false;
  }
  for (int index = 0; index < argc; ++index) {
    if (argv[index] == nullptr) {
      return false;
    }
  }
  return true;
}

bool StartsWithBridgePrefix(const char *value) noexcept {
  static const char prefix[] = "--ror-render-bridge-";
  if (value == nullptr) {
    return false;
  }
  const char *expected = prefix;
  while (*expected != '\0') {
    if (*value != *expected) {
      return false;
    }
    ++value;
    ++expected;
  }
  return true;
}

bool ConvertArguments(
    int argc, char *const argv[],
    std::vector<RendererChildLauncherString> &owned,
    std::vector<const RendererChildLauncherChar *> &pointers) {
  owned.clear();
  pointers.clear();
  owned.reserve(static_cast<std::size_t>(argc));
  for (int argument_index = 0; argument_index < argc; ++argument_index) {
    RendererChildLauncherString converted;
    const unsigned char *source = reinterpret_cast<const unsigned char *>(
        argv[argument_index]);
    while (*source != 0U) {
      converted.push_back(static_cast<RendererChildLauncherChar>(*source));
      ++source;
    }
    owned.push_back(std::move(converted));
  }
  pointers.reserve(owned.size());
  for (const RendererChildLauncherString &argument : owned) {
    pointers.push_back(argument.c_str());
  }
  return pointers.size() == static_cast<std::size_t>(argc);
}

RendererOgre14GameBridgeResult MakeResult(
    RendererOgre14GameBridgeStatus status,
    RendererBridgeEndpointArgvStatus endpoint_status,
    const RendererBridgeChannelResult &channel, bool accepted,
    bool active) noexcept {
  RendererOgre14GameBridgeResult result;
  result.status = status;
  result.endpoint_status = endpoint_status;
  result.channel = channel;
  result.accepted = accepted;
  result.active = active;
  return result;
}

} // namespace

RendererOgre14GameBridge::~RendererOgre14GameBridge() { (void)Close(); }

void RendererOgre14GameBridge::ClearForwardedArguments() noexcept {
  forwarded_argv_ = nullptr;
  forwarded_argc_ = 0;
  forwarded_pointers_.clear();
  forwarded_storage_.clear();
}

bool RendererOgre14GameBridge::StoreForwardedArguments(
    const std::vector<RendererChildLauncherString> &arguments,
    int original_argc, char *const original_argv[]) noexcept {
  try {
    constexpr int kBridgeRecordCount =
        static_cast<int>(kRendererBridgeEndpointArgvRecordCount);
    if (arguments.empty() ||
        arguments.size() > static_cast<std::size_t>(
                               (std::numeric_limits<int>::max)()) ||
        original_argc <= kBridgeRecordCount || original_argv == nullptr ||
        arguments.size() !=
            static_cast<std::size_t>(original_argc - kBridgeRecordCount)) {
      return false;
    }
    forwarded_storage_.reserve(arguments.size());
    // argv[0] and the game suffix are copied from the original narrow vector.
    // Only the six ASCII bridge records are removed; no game argument is
    // round-tripped through a Windows code-page conversion.
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
      const int original_index =
          index == 0U ? 0 : static_cast<int>(index) + kBridgeRecordCount;
      if (original_index < 0 || original_index >= original_argc ||
          original_argv[original_index] == nullptr) {
        return false;
      }
      const char *source = original_argv[original_index];
      std::vector<char> storage;
      while (*source != '\0') {
        storage.push_back(*source);
        ++source;
      }
      storage.push_back('\0');
      forwarded_storage_.push_back(std::move(storage));
    }
    forwarded_pointers_.reserve(forwarded_storage_.size());
    for (std::vector<char> &argument : forwarded_storage_) {
      if (argument.empty()) {
        return false;
      }
      forwarded_pointers_.push_back(argument.data());
    }
    forwarded_argc_ = static_cast<int>(forwarded_pointers_.size());
    forwarded_argv_ = forwarded_pointers_.data();
    return forwarded_argc_ >= 1 && forwarded_argv_ != nullptr;
  } catch (...) {
    ClearForwardedArguments();
    return false;
  }
}

RendererOgre14GameBridgeResult RendererOgre14GameBridge::Initialize(
    int argc, char **argv) noexcept {
  RendererBridgeChannelResult channel_result;
  channel_result.status = RendererBridgeChannelStatus::UNINITIALIZED;
  if (initialized_) {
    return MakeResult(RendererOgre14GameBridgeStatus::REJECTED_NOT_READY,
                      RendererBridgeEndpointArgvStatus::REJECTED_INVALID_ARGUMENTS,
                      channel_result, false, active_);
  }
  initialized_ = true;
  if (!HasValidArguments(argc, argv)) {
    status_ =
        RendererOgre14GameBridgeStatus::REJECTED_INVALID_ARGUMENT_VECTOR;
    return MakeResult(status_,
                      RendererBridgeEndpointArgvStatus::REJECTED_INVALID_ARGUMENTS,
                      channel_result, false, false);
  }
  if (argc < 2 || !StartsWithBridgePrefix(argv[1])) {
    status_ = RendererOgre14GameBridgeStatus::LEGACY_DIRECT;
    forwarded_argc_ = argc;
    forwarded_argv_ = argv;
    return MakeResult(status_,
                      RendererBridgeEndpointArgvStatus::REJECTED_MISSING_CONTRACT,
                      channel_result, true, false);
  }

  try {
    std::vector<RendererChildLauncherString> native_arguments;
    std::vector<const RendererChildLauncherChar *> native_pointers;
    if (!ConvertArguments(argc, argv, native_arguments, native_pointers)) {
      status_ = RendererOgre14GameBridgeStatus::FAILED_INTERNAL;
      return MakeResult(status_,
                        RendererBridgeEndpointArgvStatus::FAILED_INTERNAL,
                        channel_result, false, false);
    }
    const RendererBridgeEndpointArgvParseResult parsed =
        ParseRendererBridgeEndpoint(
            static_cast<int>(native_pointers.size()),
            native_pointers.data());
    if (!parsed.accepted) {
      status_ =
          RendererOgre14GameBridgeStatus::REJECTED_MALFORMED_ENDPOINT;
      return MakeResult(status_, parsed.status, channel_result, false, false);
    }
    if (parsed.endpoint.role != RendererBridgeRole::GAME_HOST) {
      status_ = RendererOgre14GameBridgeStatus::REJECTED_WRONG_ROLE;
      return MakeResult(status_, parsed.status, channel_result, false, false);
    }
    if (!StoreForwardedArguments(parsed.forwarded_arguments, argc, argv)) {
      status_ = RendererOgre14GameBridgeStatus::FAILED_INTERNAL;
      return MakeResult(status_, parsed.status, channel_result, false, false);
    }

    endpoint_ = parsed.endpoint;
    channel_.reset(new RendererBridgeChannel(endpoint_));
    channel_result = channel_->Adopt();
    if (!channel_result) {
      status_ = RendererOgre14GameBridgeStatus::FAILED_CHANNEL_ADOPTION;
      channel_.reset();
      ClearForwardedArguments();
      return MakeResult(status_, parsed.status, channel_result, false, false);
    }
    status_ = RendererOgre14GameBridgeStatus::READY;
    active_ = true;
    return MakeResult(status_, parsed.status, channel_result, true, true);
  } catch (...) {
    status_ = RendererOgre14GameBridgeStatus::FAILED_INTERNAL;
    channel_.reset();
    ClearForwardedArguments();
    return MakeResult(status_, RendererBridgeEndpointArgvStatus::FAILED_INTERNAL,
                      channel_result, false, false);
  }
}

RendererBridgeChannelResult RendererOgre14GameBridge::Close() noexcept {
  RendererBridgeChannelResult result;
  if (channel_ != nullptr) {
    result = channel_->Close();
    channel_.reset();
  } else {
    result.status = RendererBridgeChannelStatus::CLOSED;
  }
  active_ = false;
  ClearForwardedArguments();
  return result;
}

const RendererBridgeEndpoint *RendererOgre14GameBridge::endpoint() const
    noexcept {
  return active_ ? &endpoint_ : nullptr;
}

bool IsKnownRendererOgre14GameBridgeStatus(
    RendererOgre14GameBridgeStatus status) noexcept {
  switch (status) {
  case RendererOgre14GameBridgeStatus::LEGACY_DIRECT:
  case RendererOgre14GameBridgeStatus::READY:
  case RendererOgre14GameBridgeStatus::REJECTED_INVALID_ARGUMENT_VECTOR:
  case RendererOgre14GameBridgeStatus::REJECTED_MALFORMED_ENDPOINT:
  case RendererOgre14GameBridgeStatus::REJECTED_WRONG_ROLE:
  case RendererOgre14GameBridgeStatus::REJECTED_NOT_READY:
  case RendererOgre14GameBridgeStatus::FAILED_CHANNEL_ADOPTION:
  case RendererOgre14GameBridgeStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererOgre14GameBridgeStatus status) noexcept {
  switch (status) {
  case RendererOgre14GameBridgeStatus::LEGACY_DIRECT:
    return "legacy-direct";
  case RendererOgre14GameBridgeStatus::READY:
    return "ready";
  case RendererOgre14GameBridgeStatus::REJECTED_INVALID_ARGUMENT_VECTOR:
    return "rejected-invalid-argument-vector";
  case RendererOgre14GameBridgeStatus::REJECTED_MALFORMED_ENDPOINT:
    return "rejected-malformed-endpoint";
  case RendererOgre14GameBridgeStatus::REJECTED_WRONG_ROLE:
    return "rejected-wrong-role";
  case RendererOgre14GameBridgeStatus::REJECTED_NOT_READY:
    return "rejected-not-ready";
  case RendererOgre14GameBridgeStatus::FAILED_CHANNEL_ADOPTION:
    return "failed-channel-adoption";
  case RendererOgre14GameBridgeStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR

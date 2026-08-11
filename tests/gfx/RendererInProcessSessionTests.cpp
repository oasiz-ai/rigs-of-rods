/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererInProcessSession.h"

#if defined(OGRE_VERSION) || defined(OGRE_VERSION_MAJOR) ||                    \
    defined(OGRE_PLATFORM) || defined(_OgrePrerequisites_H_) ||                \
    defined(__OgrePrerequisites_H__) || defined(_OgreRoot_H_)
#error "in-process session public API imported a renderer SDK"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace RoR;
using namespace RoR::Render;

static_assert(!std::is_copy_constructible_v<RendererInProcessSession>);
static_assert(!std::is_move_constructible_v<RendererInProcessSession>);
static_assert(std::is_abstract_v<IRendererInProcessEventPump>);

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "in-process session test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

Matrix4x4 Perspective(float near_plane, float far_plane) {
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.0F;
  const float depth_scale = far_plane / (near_plane - far_plane);
  projection.elements[10U] = depth_scale;
  projection.elements[11U] = -1.0F;
  projection.elements[14U] = near_plane * depth_scale;
  return projection;
}

FrontendSurfaceUpdate Surface(std::uint64_t revision, std::uint32_t width,
                              std::uint32_t height) {
  FrontendSurfaceUpdate surface;
  surface.surface_revision = revision;
  surface.window.system = NativeWindowSystem::COCOA;
  surface.window.surface = 0x1234U;
  surface.window.generation = 1U;
  surface.pixel_width = width;
  surface.pixel_height = height;
  surface.content_scale = {2.0F, 2.0F};
  Require(ValidateFrontendSurfaceUpdate(surface, false).ok(),
          "surface fixture is invalid");
  return surface;
}

RendererInProcessSessionConfig Config(std::uint64_t registry_id) {
  RendererInProcessSessionConfig config;
  config.frontend.initial_surface_revision = 1U;
  config.frontend.window = Surface(1U, 800U, 600U).window;
  config.frontend.initial_width = 800U;
  config.frontend.initial_height = 600U;
  config.frontend.initial_content_scale = {2.0F, 2.0F};
  config.frontend.maximum_frames_in_flight = 2U;
  config.frontend.headless = false;
  config.frontend.vertical_sync = true;
  config.producer.registry_id = registry_id;
  config.surface_update_timeout_nanoseconds = 50'000'000U;
  config.shutdown_timeout_nanoseconds = 50'000'000U;
  config.present_frames = true;
  Require(ValidateFrontendInitializationRequest(config.frontend).ok(),
          "frontend config fixture is invalid");
  return config;
}

class FakeSceneSource final : public IJoinedGraphicsSceneSource {
public:
  explicit FakeSceneSource(std::vector<std::string> &shared_log)
      : log(shared_log) {
    frame.simulation_tick = 41U;
    frame.simulation_time_seconds = 1.0;
    GraphicsSceneLightInput sun;
    sun.source_light_id = 7U;
    frame.lights.push_back(sun);
    frame.camera.view_id = 1U;
    frame.camera.width = 640U;
    frame.camera.height = 480U;
    frame.camera.clip_from_view =
        Perspective(frame.camera.near_plane, frame.camera.far_plane);
  }

  ValidationResult
  CaptureJoinedGraphicsFrame(GraphicsSceneFrameInput &output) override {
    log.emplace_back("capture");
    ++captures;
    if (throw_on_capture) {
      throw std::runtime_error("injected joined-source failure");
    }
    output = frame;
    return capture_result;
  }
  void CommitJoinedGraphicsFrame() noexcept override {
    log.emplace_back("commit");
    ++commits;
  }
  void DiscardJoinedGraphicsFrame() noexcept override {
    log.emplace_back("discard");
    ++discards;
  }

  std::vector<std::string> &log;
  GraphicsSceneFrameInput frame;
  ValidationResult capture_result = ValidationResult::Success();
  std::uint32_t captures = 0U;
  std::uint32_t commits = 0U;
  std::uint32_t discards = 0U;
  bool throw_on_capture = false;
};

class FakeEventPump final : public IRendererInProcessEventPump {
public:
  struct Step final {
    RendererInProcessEventPollPoint point =
        RendererInProcessEventPollPoint::BEFORE_SIMULATION;
    RendererInProcessEventObservation observation;
    ValidationResult result = ValidationResult::Success();
  };

  explicit FakeEventPump(std::vector<std::string> &shared_log)
      : log(shared_log) {}

  void Push(RendererInProcessEventPollPoint point) {
    Step step;
    step.point = point;
    steps.push_back(std::move(step));
  }

  void PushSurface(RendererInProcessEventPollPoint point,
                   FrontendSurfaceUpdate surface) {
    Step step;
    step.point = point;
    step.observation.surface_update = std::move(surface);
    steps.push_back(std::move(step));
  }

  ValidationResult PollEvents(
      RendererInProcessEventPollPoint point,
      RendererInProcessEventObservation &observation) override {
    log.emplace_back(point ==
                             RendererInProcessEventPollPoint::BEFORE_SIMULATION
                         ? "events-before-simulation"
                         : "events-before-present");
    ++polls;
    if (steps.empty() || steps.front().point != point) {
      return ValidationResult::Failure(
          ValidationCode::NON_DETERMINISTIC_ORDER, "fake_event_step",
          "unexpected event-poll phase");
    }
    Step step = std::move(steps.front());
    steps.pop_front();
    observation = std::move(step.observation);
    return step.result;
  }

  void ShutdownEventPump() noexcept override {
    log.emplace_back("event-shutdown");
    ++shutdowns;
  }

  std::vector<std::string> &log;
  std::deque<Step> steps;
  std::uint32_t polls = 0U;
  std::uint32_t shutdowns = 0U;
};

class FakeFramePolicy final : public IRendererInProcessFramePolicy {
public:
  ValidationResult BeginCapture(std::uint32_t,
                                std::uint32_t) override {
    Require(!capture_active, "fake frame policy was entered recursively");
    capture_active = true;
    ++capture_begins;
    return ValidationResult::Success();
  }

  void EndCapture() noexcept override {
    capture_active = false;
    ++capture_ends;
  }

  ValidationResult NormalizeAndValidate(
      GraphicsSceneFrameInput &frame,
      std::uint32_t drawable_width,
      std::uint32_t drawable_height) override {
    Require(!capture_active,
            "frame normalization ran inside the pre-capture scope");
    if (throw_after_capture) {
      throw std::runtime_error("injected frame-policy failure");
    }
    if (frame.lights.size() != 1U ||
        frame.lights.front().type != LightType::DIRECTIONAL ||
        frame.lights.front().source_light_id == 0U) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE, "fake_frame_policy.sun",
          "fixture requires one directional sun");
    }
    if (drawable_width == 0U || drawable_height == 0U ||
        frame.camera.clip_from_view.elements[11U] != -1.0F ||
        frame.camera.clip_from_view.elements[15U] != 0.0F) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_DIMENSIONS, "fake_frame_policy.camera",
          "fixture requires an active perspective camera");
    }
    frame.camera.width = drawable_width;
    frame.camera.height = drawable_height;
    frame.camera.near_plane = 0.5F;
    frame.camera.far_plane = 350.0F;
    frame.camera.clip_from_view.elements[0U] =
        frame.camera.clip_from_view.elements[5U] /
        (static_cast<float>(drawable_width) /
         static_cast<float>(drawable_height));
    frame.camera.clip_from_view.elements[10U] =
        frame.camera.far_plane /
        (frame.camera.near_plane - frame.camera.far_plane);
    frame.camera.clip_from_view.elements[14U] =
        frame.camera.near_plane *
        frame.camera.clip_from_view.elements[10U];
    return ValidationResult::Success();
  }

  bool throw_after_capture = false;
  bool capture_active = false;
  std::uint32_t capture_begins = 0U;
  std::uint32_t capture_ends = 0U;
};

class FakeFrontend final : public IRendererFrontend {
public:
  explicit FakeFrontend(std::vector<std::string> &shared_log)
      : log(shared_log) {
    capabilities.frontend_kind = RendererFrontendKind::CUSTOM;
    capabilities.raster_api = RasterGraphicsApi::METAL;
    capabilities.native_api = NativeGraphicsApi::NONE;
    capabilities.frontend_name = "in-process-fake";
    capabilities.frontend_version = "1";
    capabilities.maximum_texture_dimension_2d = 8192U;
    capabilities.maximum_views = 1U;
    capabilities.maximum_frames_in_flight = 2U;
    capabilities.supported_outputs = FrameOutputMask::COLOR;
    capabilities.raster_ready = true;
    Require(ValidateFrontendCapabilityReport(capabilities).ok(),
            "frontend capability fixture is invalid");
  }

  FrontendCapabilityReport QueryCapabilities() const override {
    return capabilities;
  }

  RenderOperationResult
  Initialize(const FrontendInitializationRequest &request) override {
    log.emplace_back("frontend-initialize");
    ++initializations;
    current_surface = Surface(request.initial_surface_revision,
                              request.initial_width, request.initial_height);
    current_surface.window = request.window;
    current_surface.content_scale = request.initial_content_scale;
    initialized = true;
    return RenderOperationResult::Success();
  }

  RenderOperationResult UpdateSurface(const FrontendSurfaceUpdate &update,
                                      bool headless,
                                      std::uint64_t) override {
    log.emplace_back("surface-update");
    ++surface_updates;
    if (surface_update_timeouts != 0U) {
      --surface_update_timeouts;
      return RenderOperationResult::Failure(RenderOperationCode::TIMEOUT,
                                            "injected drain timeout");
    }
    const ValidationResult transition = ValidateFrontendSurfaceTransition(
        current_surface, update, headless, true);
    if (!transition) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT, transition.detail);
    }
    current_surface = update;
    return RenderOperationResult::Success();
  }

  RenderOperationResult
  SynchronizeAssets(const RenderAssetDelta &delta) override {
    log.emplace_back("asset");
    synchronized_sequences.push_back(delta.sequence);
    return RenderOperationResult::Success();
  }

  RenderOperationResult
  ResetSceneGeneration(std::uint64_t next_generation) override {
    log.emplace_back("generation-reset");
    reset_generations.push_back(next_generation);
    return RenderOperationResult::Success();
  }

  RenderOperationResult ReleaseResource(ResourceHandle resource) override {
    log.emplace_back("release");
    released.push_back(resource);
    return RenderOperationResult::Success();
  }

  RenderOperationResult Render(const RenderFrameRequest &request,
                               RenderFrameOutput &output) override {
    log.emplace_back("scene");
    if (request.present) {
      const ValidationResult presentation =
          ValidateRenderFramePresentation(request, current_surface);
      if (!presentation) {
        return RenderOperationResult::Failure(
            RenderOperationCode::INVALID_ARGUMENT, presentation.detail);
      }
    }
    rendered.push_back(request);
    output.frame_id = request.frame_id;
    output.snapshot_id = request.scene_snapshot->snapshot_id();
    output.status = RenderFrameStatus::RENDERED;
    output.presented = request.present;
    output.presented_view_id = request.presentation_view_id;
    FrameAttachment attachment;
    attachment.view_id = request.views.front().view_id;
    attachment.output = FrameOutputMask::COLOR;
    attachment.format = request.color_format;
    attachment.width = request.views.front().width;
    attachment.height = request.views.front().height;
    attachment.gpu_resource = ResourceHandle::Create(
        ResourceKind::RENDER_TARGET, 77U,
        static_cast<std::uint32_t>(request.frame_id), 1U);
    output.attachments.push_back(std::move(attachment));
    return RenderOperationResult::Success();
  }

  bool IsFrameComplete(std::uint64_t) const noexcept override { return true; }

  RenderOperationResult WaitForFrame(std::uint64_t,
                                     std::uint64_t) override {
    log.emplace_back("wait");
    return RenderOperationResult::Success();
  }

  NativeRenderInterop *GetNativeInterop() noexcept override { return nullptr; }

  RenderOperationResult Shutdown(std::uint64_t) override {
    log.emplace_back("frontend-shutdown");
    ++shutdowns;
    if (shutdown_timeouts != 0U) {
      --shutdown_timeouts;
      return RenderOperationResult::Failure(RenderOperationCode::TIMEOUT,
                                            "injected shutdown timeout");
    }
    initialized = false;
    return RenderOperationResult::Success();
  }

  std::vector<std::string> &log;
  FrontendCapabilityReport capabilities;
  FrontendSurfaceUpdate current_surface;
  std::vector<std::uint64_t> synchronized_sequences;
  std::vector<RenderFrameRequest> rendered;
  std::vector<ResourceHandle> released;
  std::vector<std::uint64_t> reset_generations;
  std::uint32_t initializations = 0U;
  std::uint32_t surface_updates = 0U;
  std::uint32_t surface_update_timeouts = 0U;
  std::uint32_t shutdowns = 0U;
  std::uint32_t shutdown_timeouts = 0U;
  bool initialized = false;
};

std::size_t FindAfter(const std::vector<std::string> &log,
                      const char *value, std::size_t after = 0U) {
  const auto found = std::find(log.begin() +
                                   static_cast<std::ptrdiff_t>(after),
                               log.end(), value);
  Require(found != log.end(), "expected call is absent from ordered log");
  return static_cast<std::size_t>(found - log.begin());
}

void TestCaptureRollbackAndTypedSubmissionOrder() {
  std::vector<std::string> log;
  FakeFrontend frontend(log);
  FakeEventPump events(log);
  FakeFramePolicy frame_policy;
  RendererInProcessSession session(frontend, events, frame_policy);
  const std::uint64_t registry_id = 0x494E50524F434553ULL;
  Require(session.Start(Config(registry_id)).status ==
              RendererInProcessSessionStatus::READY,
          "session did not initialize");
  FakeSceneSource source(log);

  source.frame.lights.clear();
  events.Push(RendererInProcessEventPollPoint::BEFORE_SIMULATION);
  Require(session.PumpEventsBeforeSimulation().simulation_may_advance,
          "invalid-frame simulation grant was not established");
  const RendererInProcessSessionResult rejected =
      session.PostUpdatedScene(source);
  Require(rejected.status ==
                  RendererInProcessSessionStatus::CAPTURE_REJECTED &&
              !rejected.terminal && source.captures == 1U &&
              source.commits == 0U && source.discards == 1U &&
              frame_policy.capture_begins == 1U &&
              frame_policy.capture_ends == 1U &&
              session.asset_sequence() == 0U &&
              session.last_consumed_scene_snapshot_id() == 0U,
          "invalid joined capture did not roll back source and producer state");

  GraphicsSceneLightInput sun;
  sun.source_light_id = 7U;
  source.frame.lights.push_back(sun);
  log.clear();
  events.Push(RendererInProcessEventPollPoint::BEFORE_SIMULATION);
  Require(session.PumpEventsBeforeSimulation().simulation_may_advance,
          "valid-frame simulation grant was not established");
  events.Push(RendererInProcessEventPollPoint::BEFORE_PRESENT);
  const RendererInProcessSessionResult submitted =
      session.PostUpdatedScene(source);
  Require(submitted.status ==
                  RendererInProcessSessionStatus::FRAME_COMPLETED &&
              submitted.ok() && submitted.asset_sequence == 1U &&
              submitted.scene_snapshot_id == 1U &&
              submitted.frontend_frame_id == 1U &&
              source.captures == 2U && source.commits == 1U &&
              source.discards == 1U &&
              frame_policy.capture_begins == 2U &&
              frame_policy.capture_ends == 2U,
          "valid retry did not retain first global asset/snapshot/frame IDs");
  const std::size_t event = FindAfter(log, "events-before-simulation");
  const std::size_t capture = FindAfter(log, "capture", event + 1U);
  const std::size_t asset = FindAfter(log, "asset", capture + 1U);
  const std::size_t scene = FindAfter(log, "scene", asset + 1U);
  Require(event < capture && capture < asset && asset < scene &&
              frontend.rendered.size() == 1U &&
              frontend.rendered.front().views.front().width == 800U &&
              frontend.rendered.front().views.front().height == 600U &&
              frontend.rendered.front().views.front().near_plane == 0.5F &&
              frontend.rendered.front().views.front().far_plane == 350.0F,
          "event/capture/asset/scene order or camera normalization changed");

  Require(session.Shutdown().ok(), "ordered submission session did not close");
}

void TestThrowingPostCapturePolicyDiscardsSourceTransaction() {
  {
    std::vector<std::string> log;
    FakeFrontend frontend(log);
    FakeEventPump events(log);
    FakeFramePolicy frame_policy;
    frame_policy.throw_after_capture = true;
    RendererInProcessSession session(frontend, events, frame_policy);
    Require(session.Start(Config(0x5448524F57504F4CULL)).ok(),
            "throwing-policy session did not initialize");
    FakeSceneSource source(log);
    events.Push(RendererInProcessEventPollPoint::BEFORE_SIMULATION);
    Require(session.PumpEventsBeforeSimulation().simulation_may_advance,
            "throwing-policy simulation grant was not established");

    const RendererInProcessSessionResult failed =
        session.PostUpdatedScene(source);
    Require(failed.status ==
                    RendererInProcessSessionStatus::FAILED_INTERNAL &&
                failed.terminal && session.terminal() &&
                source.captures == 1U && source.commits == 0U &&
                source.discards == 1U &&
                frame_policy.capture_begins == 1U &&
                frame_policy.capture_ends == 1U &&
                !session.has_pending_frame() &&
                session.asset_sequence() == 0U,
            "throwing post-capture policy stranded the source transaction");
    (void)session.Shutdown();
  }
  {
    std::vector<std::string> log;
    FakeFrontend frontend(log);
    FakeEventPump events(log);
    FakeFramePolicy frame_policy;
    RendererInProcessSession session(frontend, events, frame_policy);
    Require(session.Start(Config(0x5448524F57535243ULL)).ok(),
            "throwing-source session did not initialize");
    FakeSceneSource source(log);
    source.throw_on_capture = true;
    events.Push(RendererInProcessEventPollPoint::BEFORE_SIMULATION);
    Require(session.PumpEventsBeforeSimulation().simulation_may_advance,
            "throwing-source simulation grant was not established");

    const RendererInProcessSessionResult failed =
        session.PostUpdatedScene(source);
    Require(failed.status ==
                    RendererInProcessSessionStatus::FAILED_INTERNAL &&
                failed.terminal && session.terminal() &&
                source.captures == 1U && source.commits == 0U &&
                source.discards == 1U &&
                frame_policy.capture_begins == 1U &&
                frame_policy.capture_ends == 1U &&
                !session.has_pending_frame(),
            "throwing joined source stranded its transaction");
    (void)session.Shutdown();
  }
}

void TestSurfaceBackpressureRetainsAndRetiresExactCapture() {
  std::vector<std::string> log;
  FakeFrontend frontend(log);
  FakeEventPump events(log);
  FakeFramePolicy frame_policy;
  RendererInProcessSession session(frontend, events, frame_policy);
  Require(session.Start(Config(0x524553495A455631ULL)).ok(),
          "resize session did not initialize");
  FakeSceneSource source(log);
  frontend.surface_update_timeouts = 1U;
  events.Push(RendererInProcessEventPollPoint::BEFORE_SIMULATION);
  Require(session.PumpEventsBeforeSimulation().simulation_may_advance,
          "resize-frame simulation grant was not established");
  events.PushSurface(RendererInProcessEventPollPoint::BEFORE_PRESENT,
                     Surface(2U, 1024U, 768U));

  const RendererInProcessSessionResult pending =
      session.PostUpdatedScene(source);
  Require(pending.status ==
                  RendererInProcessSessionStatus::PENDING_BACKPRESSURE &&
              !pending.terminal && pending.pending_frame &&
              session.has_pending_frame() && source.captures == 1U &&
              source.commits == 1U && source.discards == 0U &&
              frontend.synchronized_sequences.empty() &&
              frontend.rendered.empty(),
          "surface timeout did not retain exact immutable production");

  events.Push(RendererInProcessEventPollPoint::BEFORE_SIMULATION);
  const RendererInProcessSessionResult retired =
      session.PumpEventsBeforeSimulation();
  Require(retired.status == RendererInProcessSessionStatus::FRAME_RETIRED &&
              retired.ok() && retired.scene_snapshot_id == 1U &&
              retired.frontend_frame_id == 0U &&
              retired.simulation_may_advance &&
              !session.has_pending_frame() && source.captures == 1U &&
              frontend.surface_updates == 2U &&
              frontend.synchronized_sequences ==
                  std::vector<std::uint64_t>{1U} &&
              frontend.rendered.empty(),
          "resize retry recaptured, reordered assets, or rendered stale extent");

  source.frame.simulation_tick = 42U;
  source.frame.simulation_time_seconds = 1.1;
  events.Push(RendererInProcessEventPollPoint::BEFORE_PRESENT);
  const RendererInProcessSessionResult next =
      session.PostUpdatedScene(source);
  Require(next.status == RendererInProcessSessionStatus::FRAME_COMPLETED &&
              next.scene_snapshot_id == 2U && next.frontend_frame_id == 1U &&
              source.captures == 2U && frontend.rendered.size() == 1U &&
              frontend.rendered.front().views.front().width == 1024U &&
              frontend.rendered.front().views.front().height == 768U,
          "post-resize frame did not resume exact snapshot/frame lineage");

  events.Push(RendererInProcessEventPollPoint::BEFORE_PRESENT);
  const RendererInProcessSessionResult reset =
      session.ResetSceneGeneration();
  Require(reset.status ==
                  RendererInProcessSessionStatus::SCENE_GENERATION_RESET &&
              reset.ok() && reset.scene_snapshot_id == 3U &&
              session.scene_generation() == 2U &&
              session.last_consumed_scene_snapshot_id() == 3U &&
              session.last_frontend_frame_id() == 2U &&
              frontend.reset_generations == std::vector<std::uint64_t>{2U},
          "map reset replaced dispatcher or reset process-lifetime IDs");

  source.frame.simulation_tick = 0U;
  source.frame.simulation_time_seconds = 0.0;
  events.Push(RendererInProcessEventPollPoint::BEFORE_SIMULATION);
  Require(session.PumpEventsBeforeSimulation().simulation_may_advance,
          "reloaded-frame simulation grant was not established");
  events.Push(RendererInProcessEventPollPoint::BEFORE_PRESENT);
  const RendererInProcessSessionResult reloaded =
      session.PostUpdatedScene(source);
  Require(reloaded.status ==
                  RendererInProcessSessionStatus::FRAME_COMPLETED &&
              reloaded.scene_snapshot_id == 4U &&
              reloaded.frontend_frame_id == 3U &&
              session.scene_generation() == 2U,
          "new map did not preserve global snapshot/frontend frame identity");

  frontend.shutdown_timeouts = 1U;
  const RendererInProcessSessionResult timed_out = session.Shutdown();
  Require(timed_out.status ==
                  RendererInProcessSessionStatus::FAILED_FRONTEND_SHUTDOWN &&
              !timed_out.terminal && events.shutdowns == 0U,
          "frontend shutdown timeout released native event/window ownership");
  const RendererInProcessSessionResult closed = session.Shutdown();
  Require(closed.status == RendererInProcessSessionStatus::CLOSED &&
              closed.ok() && events.shutdowns == 1U &&
              session.last_consumed_scene_snapshot_id() == 4U &&
              session.last_frontend_frame_id() == 3U,
          "shutdown retry did not release frontend before event pump");
  const std::size_t frontend_shutdown =
      FindAfter(log, "frontend-shutdown", 1U);
  const std::size_t event_shutdown =
      FindAfter(log, "event-shutdown", frontend_shutdown + 1U);
  Require(frontend_shutdown < event_shutdown,
          "event/window owner shut down before frontend released its borrow");
  Require(session.Shutdown().ok() && events.shutdowns == 1U,
          "closed session shutdown was not idempotent");
}

void TestStatusSurface() {
  for (unsigned int value = 0U; value <= 255U; ++value) {
    const auto status =
        static_cast<RendererInProcessSessionStatus>(value);
    Require(IsKnownRendererInProcessSessionStatus(status) ==
                (value <= static_cast<unsigned int>(
                              RendererInProcessSessionStatus::
                                  FAILED_INTERNAL)),
            "status classifier changed");
  }
  Require(std::string(ToString(
              RendererInProcessSessionStatus::PENDING_BACKPRESSURE)) ==
              "pending_backpressure" &&
              std::string(ToString(static_cast<
                              RendererInProcessSessionStatus>(255U))) ==
                  "invalid",
          "status string mapping changed");
}

} // namespace

int main() {
  TestStatusSurface();
  TestCaptureRollbackAndTypedSubmissionOrder();
  TestThrowingPostCapturePolicyDiscardsSourceTransaction();
  TestSurfaceBackpressureRetainsAndRetiresExactCapture();
  std::cout << "renderer in-process session tests passed\n";
  return EXIT_SUCCESS;
}

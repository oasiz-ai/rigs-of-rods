/*
    This source file is part of Rigs of Rods
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ApplicationFatalError.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{

namespace fs = std::filesystem;

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::string ReadFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    Require(input.good(), "could not open " + path.string());
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

bool IsInProcessSource(const fs::path& path)
{
    static const std::unordered_set<std::string> extensions = {
        ".c", ".cc", ".cp", ".cpp", ".cxx", ".c++",
        ".h", ".hh", ".hp", ".hpp", ".hxx", ".h++",
        ".i", ".ii", ".in", ".inc", ".inl", ".ipp", ".tcc", ".tpp",
        ".m", ".mi", ".mm", ".mii",
        ".ixx", ".cppm", ".mpp", ".cu", ".cuh", ".rc"
    };

    std::string extension = path.extension().string();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return extensions.find(extension) != extensions.end();
}

bool ContainsDirectExitCall(const std::string& source)
{
    enum class LexicalState
    {
        CODE,
        LINE_COMMENT,
        BLOCK_COMMENT,
        STRING_LITERAL,
        CHARACTER_LITERAL
    };

    LexicalState state = LexicalState::CODE;
    bool escaped = false;
    for (std::size_t index = 0; index < source.size(); ++index)
    {
        const char character = source[index];
        const char next = index + 1U < source.size()
            ? source[index + 1U]
            : '\0';

        if (state == LexicalState::LINE_COMMENT)
        {
            if (character == '\n')
                state = LexicalState::CODE;
            continue;
        }
        if (state == LexicalState::BLOCK_COMMENT)
        {
            if (character == '*' && next == '/')
            {
                state = LexicalState::CODE;
                ++index;
            }
            continue;
        }
        if (state == LexicalState::STRING_LITERAL ||
            state == LexicalState::CHARACTER_LITERAL)
        {
            if (escaped)
            {
                escaped = false;
                continue;
            }
            if (character == '\\')
            {
                escaped = true;
                continue;
            }
            if ((state == LexicalState::STRING_LITERAL && character == '"') ||
                (state == LexicalState::CHARACTER_LITERAL && character == '\''))
            {
                state = LexicalState::CODE;
            }
            continue;
        }

        if (character == '/' && next == '/')
        {
            state = LexicalState::LINE_COMMENT;
            ++index;
            continue;
        }
        if (character == '/' && next == '*')
        {
            state = LexicalState::BLOCK_COMMENT;
            ++index;
            continue;
        }
        if (character == '"')
        {
            state = LexicalState::STRING_LITERAL;
            continue;
        }
        if (character == '\'')
        {
            state = LexicalState::CHARACTER_LITERAL;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(character)) == 0 &&
            character != '_')
        {
            continue;
        }

        const std::size_t identifier_start = index;
        while (index + 1U < source.size())
        {
            const unsigned char candidate =
                static_cast<unsigned char>(source[index + 1U]);
            if (std::isalnum(candidate) == 0 && candidate != '_')
                break;
            ++index;
        }
        if (source.compare(
                identifier_start,
                index - identifier_start + 1U,
                "exit") != 0)
        {
            continue;
        }

        std::size_t call = index + 1U;
        while (call < source.size() &&
               std::isspace(
                   static_cast<unsigned char>(source[call])) != 0)
        {
            ++call;
        }
        if (call < source.size() && source[call] == '(')
            return true;
    }
    return false;
}

void TestDirectExitScannerSemantics()
{
    Require(ContainsDirectExitCall("exit (124);"), "plain exit escaped scan");
    Require(ContainsDirectExitCall("::exit(124);"), "global exit escaped scan");
    Require(
        ContainsDirectExitCall("std::exit(124);"),
        "standard exit escaped scan");
    Require(
        !ContainsDirectExitCall("::_exit(125);"),
        "process-boundary _exit was classified as an in-process exit");
    Require(
        !ContainsDirectExitCall("std::_Exit(125);"),
        "standard fail-stop was classified as an in-process exit");
    Require(
        !ContainsDirectExitCall("// std::exit(124);\nint result = 0;"),
        "comment text was classified as an exit call");
    Require(
        !ContainsDirectExitCall("const char* text = \"exit(124)\";"),
        "string text was classified as an exit call");
}

struct GateFixture
{
    int calls = 0;
    bool result = false;
};

bool ReleaseGateFixture(void* opaque) noexcept
{
    GateFixture& fixture = *static_cast<GateFixture*>(opaque);
    ++fixture.calls;
    return fixture.result;
}

void TestFatalShutdownGateIsIdempotent()
{
    GateFixture success_fixture{0, true};
    RoR::ApplicationFatalShutdownGate success_gate(
        &ReleaseGateFixture,
        &success_fixture);
    Require(!success_gate.attempted(), "fresh success gate was attempted");
    Require(success_gate.Release(), "success gate rejected release");
    Require(success_gate.Release(), "success gate did not cache release");
    Require(success_gate.attempted(), "success gate did not retain attempt");
    Require(success_fixture.calls == 1, "success release ran more than once");

    GateFixture failure_fixture{0, false};
    RoR::ApplicationFatalShutdownGate failure_gate(
        &ReleaseGateFixture,
        &failure_fixture);
    Require(!failure_gate.Release(), "failure gate accepted release");
    Require(!failure_gate.Release(), "failure gate did not cache failure");
    Require(failure_fixture.calls == 1, "failed release ran more than once");

    RoR::ApplicationFatalShutdownGate null_gate(nullptr);
    Require(!null_gate.Release(), "null release gate succeeded");
    Require(null_gate.attempted(), "null release gate was not attempted");
}

void TestActiveSceneFatalShutdownSequence()
{
    std::vector<int> order;
    bool capture_active = true;
    bool presentation_active = true;
    bool workers_active = true;
    bool actors_live = true;
    bool terrain_live = true;

    const RoR::ApplicationFatalShutdownDisposition disposition =
        RoR::RunApplicationFatalShutdownSequence(
            [&]() {
                order.push_back(1);
                capture_active = false;
                return true;
            },
            [&]() {
                order.push_back(2);
                presentation_active = false;
                return true;
            },
            [&]() {
                order.push_back(3);
                workers_active = false;
                return true;
            },
            [&]() {
                order.push_back(4);
                actors_live = false;
                terrain_live = false;
                return true;
            });

    Require(
        disposition ==
            RoR::ApplicationFatalShutdownDisposition::RETURN_FROM_MAIN,
        "active-scene shutdown selected fail-stop");
    Require(
        order == std::vector<int>({1, 2, 3, 4}),
        "active-scene shutdown order changed");
    Require(
        !capture_active && !presentation_active && !workers_active &&
            !actors_live && !terrain_live,
        "active-scene shutdown retained runtime state");
}

void TestPartialTerrainFatalShutdownSequence()
{
    std::vector<int> order;
    bool partial_terrain_live = true;
    const RoR::ApplicationFatalShutdownDisposition disposition =
        RoR::RunApplicationFatalShutdownSequence(
            [&]() { order.push_back(1); return true; },
            [&]() { order.push_back(2); return true; },
            [&]() { order.push_back(3); return true; },
            [&]() {
                order.push_back(4);
                partial_terrain_live = false;
                return true;
            });

    Require(
        disposition ==
            RoR::ApplicationFatalShutdownDisposition::RETURN_FROM_MAIN,
        "partial-terrain shutdown selected fail-stop");
    Require(!partial_terrain_live, "partial terrain was not disposed");
    Require(
        order == std::vector<int>({1, 2, 3, 4}),
        "partial-terrain shutdown order changed");
}

void TestWorkerJoinFailureSelectsFailStop()
{
    std::vector<int> order;
    bool scene_release_called = false;
    const RoR::ApplicationFatalShutdownDisposition disposition =
        RoR::RunApplicationFatalShutdownSequence(
            [&]() { order.push_back(1); return true; },
            [&]() { order.push_back(2); return true; },
            [&]() { order.push_back(3); return false; },
            [&]() {
                order.push_back(4);
                scene_release_called = true;
                return true;
            });

    Require(
        disposition == RoR::ApplicationFatalShutdownDisposition::FAIL_STOP,
        "worker join failure did not select fail-stop");
    Require(!scene_release_called, "scene released after failed worker join");
    Require(
        order == std::vector<int>({1, 2, 3}),
        "worker join failure did not stop the shutdown sequence");
}

void TestFatalErrorValueSemantics()
{
    try
    {
        throw RoR::ApplicationFatalError(124, "outdated ground model");
    }
    catch (const RoR::ApplicationFatalError& fatal)
    {
        Require(fatal.exit_code() == 124, "fatal exit code changed");
        Require(
            std::string(fatal.what()) == "outdated ground model",
            "fatal reason changed");
    }

    const RoR::ApplicationFatalError null_reason(126, nullptr);
    Require(null_reason.exit_code() == 126, "second fatal code changed");
    Require(
        std::string(null_reason.what()) == "fatal application error",
        "null fatal reason was not normalized");
}

void TestNoDirectInProcessExit(const fs::path& repository_root)
{
    const fs::path source_root = repository_root / "source" / "main";
    Require(fs::is_directory(source_root), "source/main is unavailable");

    for (const fs::directory_entry& entry :
         fs::recursive_directory_iterator(source_root))
    {
        if (!entry.is_regular_file() || !IsInProcessSource(entry.path()))
            continue;

        const std::string source = ReadFile(entry.path());
        // Direct exit, ::exit and std::exit all contain the identifier token
        // `exit`. POSIX `_exit` and standard `_Exit` are distinct fail-stop
        // primitives; the controlled fatal path may use the latter only when
        // unwinding renderer/archive ownership is demonstrably unsafe.
        if (ContainsDirectExitCall(source))
        {
            throw std::runtime_error(
                "direct in-process exit call remains in " +
                fs::relative(entry.path(), repository_root).string());
        }
    }
}

void TestFatalPropagationAndLifetimeOrder(const fs::path& repository_root)
{
    const std::string application = ReadFile(
        repository_root / "source" / "main" / "Application.cpp");
    const std::size_t content_static = application.find(
        "static ContentManager       g_content_manager;");
    const std::size_t app_static = application.find(
        "static AppContext           g_app_context;");
    Require(
        content_static != std::string::npos &&
            app_static != std::string::npos &&
            content_static < app_static,
        "ContentManager must be constructed before and destroyed after AppContext");

    const std::size_t generic_handler = application.find(
        "void HandleGenericException(");
    const std::size_t fatal_rethrow = application.find(
        "catch (const ApplicationFatalError&)", generic_handler);
    const std::size_t recoverable_ogre = application.find(
        "catch (Ogre::Exception&", generic_handler);
    const std::size_t rethrow = application.find("throw;", fatal_rethrow);
    Require(
        generic_handler != std::string::npos &&
            fatal_rethrow != std::string::npos &&
            recoverable_ogre != std::string::npos &&
            fatal_rethrow < recoverable_ogre &&
            rethrow < recoverable_ogre,
        "central exception handler does not propagate fatal control flow");

    const std::string main_source = ReadFile(
        repository_root / "source" / "main" / "main.cpp");
    const std::size_t renderer_guard = main_source.find(
        "RendererRuntimeGuard renderer_runtime_guard;");
    const std::size_t fatal_catch = main_source.find(
        "catch (const ApplicationFatalError& fatal)");
    const std::size_t fatal_sequence = main_source.find(
        "RunApplicationFatalShutdownSequence(", fatal_catch);
    const std::size_t worker_release = main_source.find(
        "worker_runtime_guard.Release()", fatal_sequence);
    const std::size_t scene_release = main_source.find(
        "fatal_scene_runtime_gate.Release()", worker_release);
    const std::size_t fail_stop = main_source.find(
        "FailStopApplication(fatal.exit_code())", scene_release);
    const std::size_t preserved_code = main_source.find(
        "application_exit_code = fatal.exit_code();", fatal_catch);
    const std::size_t controlled_return = main_source.find(
        "return application_exit_code;", preserved_code);
    Require(
        renderer_guard != std::string::npos &&
            fatal_catch != std::string::npos &&
            fatal_sequence != std::string::npos &&
            worker_release != std::string::npos &&
            scene_release != std::string::npos &&
            fail_stop != std::string::npos &&
            preserved_code != std::string::npos &&
            controlled_return != std::string::npos &&
            renderer_guard < fatal_catch &&
            fatal_catch < fatal_sequence &&
            fatal_sequence < worker_release &&
            worker_release < scene_release &&
            scene_release < fail_stop &&
            fail_stop < preserved_code &&
            preserved_code < controlled_return,
        "fatal catch does not quiesce workers/scene inside renderer lifetime");
    Require(
        main_source.find("bool ReleaseWorkerRuntime() noexcept") !=
                std::string::npos &&
            main_source.find("std::_Exit(exit_code)") != std::string::npos,
        "worker proof or process fail-stop contract is missing");

    const std::string game_context = ReadFile(
        repository_root / "source" / "main" / "GameContext.cpp");
    const std::size_t partial_fatal = game_context.find(
        "catch (const ApplicationFatalError&)");
    const std::size_t partial_dispose = game_context.find(
        "DisposeForFatalShutdown()", partial_fatal);
    const std::size_t partial_rethrow = game_context.find(
        "throw;", partial_dispose);
    const std::size_t fatal_scene = game_context.find(
        "bool GameContext::ShutdownSceneForFatalError() noexcept");
    const std::size_t actor_cleanup = game_context.find(
        "m_actor_manager.CleanUpSimulation()", fatal_scene);
    const std::size_t terrain_cleanup = game_context.find(
        "m_terrain->DisposeForFatalShutdown()", actor_cleanup);
    const std::size_t scene_clear = game_context.find(
        "App::GetGfxScene()->ClearScene()", terrain_cleanup);
    Require(
        partial_fatal != std::string::npos &&
            partial_dispose != std::string::npos &&
            partial_rethrow != std::string::npos &&
            partial_fatal < partial_dispose &&
            partial_dispose < partial_rethrow,
        "partial terrain is not disposed before fatal propagation");
    Require(
        fatal_scene != std::string::npos &&
            actor_cleanup != std::string::npos &&
            terrain_cleanup != std::string::npos &&
            scene_clear != std::string::npos &&
            fatal_scene < actor_cleanup &&
            actor_cleanup < terrain_cleanup &&
            terrain_cleanup < scene_clear,
        "active fatal scene teardown order changed");
    const std::size_t transaction_destructor = game_context.find(
        "~TerrainResourceLoadTransaction() noexcept");
    const std::size_t transaction_catch = game_context.find(
        "catch (...)", transaction_destructor);
    Require(
        transaction_destructor != std::string::npos &&
            transaction_catch != std::string::npos &&
            transaction_destructor < transaction_catch &&
            transaction_catch < partial_fatal,
        "terrain resource rollback can throw during fatal unwinding");

    const std::string terrain_header = ReadFile(
        repository_root / "source" / "main" / "terrain" / "Terrain.h");
    const std::string terrain_source = ReadFile(
        repository_root / "source" / "main" / "terrain" / "Terrain.cpp");
    Require(
        terrain_header.find("bool DisposeForFatalShutdown() noexcept;") !=
                std::string::npos &&
            terrain_header.find("m_fatal_dispose_failed") !=
                std::string::npos &&
            terrain_source.find(
                "bool RoR::Terrain::DisposeForFatalShutdown() noexcept") !=
                std::string::npos &&
            terrain_source.find("m_disposed = true;") != std::string::npos,
        "terrain fatal disposal is not latched and non-throwing");

    const std::string collisions = ReadFile(
        repository_root / "source" / "main" / "physics" /
        "collision" / "Collisions.cpp");
    const std::string actor = ReadFile(
        repository_root / "source" / "main" / "physics" / "Actor.cpp");
    Require(
        std::regex_search(
            collisions,
            std::regex(
                R"(throw[[:space:]]+ApplicationFatalError[[:space:]]*\([[:space:]]*124[[:space:]]*,)")),
        "ground-model fatal no longer preserves exit code 124");
    Require(
        std::regex_search(
            actor,
            std::regex(
                R"(throw[[:space:]]+ApplicationFatalError[[:space:]]*\([[:space:]]*126[[:space:]]*,)")),
        "network-size fatal no longer preserves exit code 126");
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "expected repository root argument");
        const fs::path repository_root = fs::absolute(argv[1]);
        TestFatalErrorValueSemantics();
        TestFatalShutdownGateIsIdempotent();
        TestActiveSceneFatalShutdownSequence();
        TestPartialTerrainFatalShutdownSequence();
        TestWorkerJoinFailureSelectsFailStop();
        TestDirectExitScannerSemantics();
        TestNoDirectInProcessExit(repository_root);
        TestFatalPropagationAndLifetimeOrder(repository_root);
        std::cout << "application fatal shutdown contract tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "application fatal shutdown contract test failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

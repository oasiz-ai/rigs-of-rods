/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#if __has_include(<AL/al.h>)
  #include <AL/al.h>
  #include <AL/alc.h>
#elif defined(__APPLE__)
  #include <OpenAL/al.h>
  #include <OpenAL/alc.h>
#else
# error "No compatible OpenAL headers found"
#endif

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int Fail(const std::string& message)
{
    std::cerr << "macOS OpenAL smoke test failed: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    // OpenAL Soft's null backend executes the real device/context/object
    // lifecycle without requiring speakers or changing the user's output.
    if (setenv("ALSOFT_DRIVERS", "null", 1) != 0 ||
        setenv("ALSOFT_LOGLEVEL", "0", 1) != 0)
    {
        return Fail("could not configure the process-local null backend");
    }

    ALCdevice* const device = alcOpenDevice(nullptr);
    if (device == nullptr)
    {
        return Fail("the OpenAL Soft null device did not open");
    }

    ALCcontext* const context = alcCreateContext(device, nullptr);
    if (context == nullptr)
    {
        alcCloseDevice(device);
        return Fail("the OpenAL Soft null context was not created");
    }
    if (alcMakeContextCurrent(context) == ALC_FALSE)
    {
        alcDestroyContext(context);
        alcCloseDevice(device);
        return Fail("the OpenAL Soft null context could not be made current");
    }

    ALuint sources[3] = {};
    ALuint buffers[2] = {};
    ALsizei source_count = 0;
    ALsizei buffer_count = 0;
    std::string failure;

    alGetError();
    alGenSources(3, sources);
    if (alGetError() != AL_NO_ERROR)
    {
        failure = "source allocation failed";
    }
    else
    {
        source_count = 3;
    }

    if (failure.empty())
    {
        alGenBuffers(2, buffers);
        if (alGetError() != AL_NO_ERROR)
        {
            failure = "buffer allocation failed";
        }
        else
        {
            buffer_count = 2;
        }
    }

    if (failure.empty())
    {
        const ALshort silence[64] = {};
        alBufferData(
            buffers[0],
            AL_FORMAT_MONO16,
            silence,
            static_cast<ALsizei>(sizeof(silence)),
            22050);
        alSourcei(sources[0], AL_BUFFER, static_cast<ALint>(buffers[0]));
        alSourcePlay(sources[0]);
        alSourceStop(sources[0]);
        if (alGetError() != AL_NO_ERROR)
        {
            failure = "buffer upload or source playback lifecycle failed";
        }
    }

    // Mirror the corrected SoundManager ownership rule: delete only handles
    // whose generation succeeded, while the owned context is current.
    if (source_count > 0)
    {
        alDeleteSources(source_count, sources);
    }
    if (buffer_count > 0)
    {
        alDeleteBuffers(buffer_count, buffers);
    }
    if (alGetError() != AL_NO_ERROR && failure.empty())
    {
        failure = "generated OpenAL objects did not tear down cleanly";
    }

    if (alcMakeContextCurrent(nullptr) == ALC_FALSE && failure.empty())
    {
        failure = "the OpenAL context could not be detached";
    }
    alcDestroyContext(context);
    if (alcCloseDevice(device) == ALC_FALSE && failure.empty())
    {
        failure = "the OpenAL device did not close cleanly";
    }

    if (!failure.empty())
    {
        return Fail(failure);
    }

    std::cout << "macOS OpenAL Soft null-backend lifecycle verified\n";
    return EXIT_SUCCESS;
}

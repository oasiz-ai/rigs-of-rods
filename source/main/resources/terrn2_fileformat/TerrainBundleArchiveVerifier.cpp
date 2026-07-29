/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "TerrainBundleArchiveVerifier.h"

#include <openssl/evp.h>

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#    include <windows.h>
#endif

namespace RoR {
namespace {

const std::size_t READ_BUFFER_BYTES = 64U * 1024U;

bool IsLowercaseSha256(const std::string& value)
{
    if (value.size() != 64U)
    {
        return false;
    }
    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        const char character = value[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f')))
        {
            return false;
        }
    }
    return true;
}

#if defined(_WIN32)
FILE* OpenArchiveReadOnly(const std::string& path)
{
    if (path.empty())
    {
        return nullptr;
    }
    const int wide_length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        path.c_str(),
        -1,
        nullptr,
        0);
    if (wide_length <= 0)
    {
        return nullptr;
    }
    std::vector<wchar_t> wide_path(
        static_cast<std::size_t>(wide_length));
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            path.c_str(),
            -1,
            &wide_path[0],
            wide_length) != wide_length)
    {
        return nullptr;
    }
    return _wfopen(&wide_path[0], L"rb");
}
#else
FILE* OpenArchiveReadOnly(const std::string& path)
{
    return std::fopen(path.c_str(), "rb");
}
#endif

struct FileCloser
{
    void operator()(FILE* file) const
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
    }
};

struct DigestContextDeleter
{
    void operator()(EVP_MD_CTX* context) const
    {
        EVP_MD_CTX_free(context);
    }
};

std::string LowercaseHex(
    const unsigned char* digest,
    std::size_t digest_size)
{
    static const char HEX_DIGITS[] = "0123456789abcdef";
    std::string result(digest_size * 2U, '0');
    for (std::size_t index = 0U; index < digest_size; ++index)
    {
        result[index * 2U] = HEX_DIGITS[digest[index] >> 4U];
        result[index * 2U + 1U] =
            HEX_DIGITS[digest[index] & 0x0fU];
    }
    return result;
}

} // namespace

bool VerifyTerrainBundleArchiveSha256(
    const std::string& archive_path,
    const std::string& expected_sha256,
    std::string& out_observed_sha256,
    std::string& out_error)
{
    out_observed_sha256.clear();
    out_error.clear();
    if (!IsLowercaseSha256(expected_sha256))
    {
        out_error =
            "expected SHA-256 must be 64 lowercase hexadecimal characters";
        return false;
    }

    std::unique_ptr<FILE, FileCloser> archive(
        OpenArchiveReadOnly(archive_path));
    if (!archive)
    {
        out_error = "could not open archive for SHA-256 verification";
        return false;
    }

    std::unique_ptr<EVP_MD_CTX, DigestContextDeleter> context(
        EVP_MD_CTX_new());
    if (!context ||
        EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
    {
        out_error = "could not initialize SHA-256 verification";
        return false;
    }

    std::array<unsigned char, READ_BUFFER_BYTES> buffer;
    for (;;)
    {
        const std::size_t bytes_read = std::fread(
            buffer.data(),
            1U,
            buffer.size(),
            archive.get());
        if (bytes_read != 0U &&
            EVP_DigestUpdate(
                context.get(),
                buffer.data(),
                bytes_read) != 1)
        {
            out_error = "could not update SHA-256 verification";
            return false;
        }
        if (bytes_read != buffer.size())
        {
            if (std::ferror(archive.get()) != 0)
            {
                out_error = "could not read archive for SHA-256 verification";
                return false;
            }
            break;
        }
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
    unsigned int digest_size = 0U;
    if (EVP_DigestFinal_ex(
            context.get(),
            digest.data(),
            &digest_size) != 1 ||
        digest_size != 32U)
    {
        out_error = "could not finalize SHA-256 verification";
        return false;
    }

    out_observed_sha256 = LowercaseHex(digest.data(), digest_size);
    if (out_observed_sha256 != expected_sha256)
    {
        out_error = "archive SHA-256 mismatch";
        return false;
    }
    return true;
}

} // namespace RoR

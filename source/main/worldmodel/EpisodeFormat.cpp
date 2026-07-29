/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "EpisodeFormat.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>

namespace RoR {
namespace WorldModel {
namespace {

const std::uint32_t SHA256_K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

std::uint32_t Ror(std::uint32_t value, unsigned int count)
{
    return (value >> count) | (value << (32U - count));
}

class Sha256
{
public:
    Sha256():
        m_state{{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U}},
        m_buffer(),
        m_bytes(0),
        m_used(0)
    {
    }

    bool Update(const void* bytes, std::size_t size)
    {
        if (size != 0 && bytes == nullptr)
            return false;
        if (size > std::numeric_limits<std::uint64_t>::max() - m_bytes)
            return false;
        const std::uint8_t* input =
            static_cast<const std::uint8_t*>(bytes);
        m_bytes += static_cast<std::uint64_t>(size);
        while (size != 0)
        {
            const std::size_t copied =
                std::min(size, m_buffer.size() - m_used);
            std::memcpy(m_buffer.data() + m_used, input, copied);
            m_used += copied;
            input += copied;
            size -= copied;
            if (m_used == m_buffer.size())
            {
                Transform(m_buffer.data());
                m_used = 0;
            }
        }
        return true;
    }

    Hash256 Finish()
    {
        const std::uint64_t bits = m_bytes * UINT64_C(8);
        m_buffer[m_used++] = 0x80U;
        if (m_used > 56)
        {
            std::fill(m_buffer.begin() + m_used, m_buffer.end(), 0);
            Transform(m_buffer.data());
            m_used = 0;
        }
        std::fill(m_buffer.begin() + m_used, m_buffer.begin() + 56, 0);
        for (unsigned int i = 0; i < 8; ++i)
            m_buffer[63U - i] =
                static_cast<std::uint8_t>(bits >> (i * 8U));
        Transform(m_buffer.data());
        Hash256 result;
        for (std::size_t word = 0; word < 8; ++word)
        {
            result.bytes[word * 4U] =
                static_cast<std::uint8_t>(m_state[word] >> 24U);
            result.bytes[word * 4U + 1U] =
                static_cast<std::uint8_t>(m_state[word] >> 16U);
            result.bytes[word * 4U + 2U] =
                static_cast<std::uint8_t>(m_state[word] >> 8U);
            result.bytes[word * 4U + 3U] =
                static_cast<std::uint8_t>(m_state[word]);
        }
        return result;
    }

private:
    void Transform(const std::uint8_t block[64])
    {
        std::uint32_t w[64];
        for (std::size_t i = 0; i < 16; ++i)
        {
            const std::size_t j = i * 4U;
            w[i] =
                (static_cast<std::uint32_t>(block[j]) << 24U) |
                (static_cast<std::uint32_t>(block[j + 1U]) << 16U) |
                (static_cast<std::uint32_t>(block[j + 2U]) << 8U) |
                static_cast<std::uint32_t>(block[j + 3U]);
        }
        for (std::size_t i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 =
                Ror(w[i - 15U], 7) ^ Ror(w[i - 15U], 18) ^
                (w[i - 15U] >> 3U);
            const std::uint32_t s1 =
                Ror(w[i - 2U], 17) ^ Ror(w[i - 2U], 19) ^
                (w[i - 2U] >> 10U);
            w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
        }
        std::uint32_t a = m_state[0], b = m_state[1];
        std::uint32_t c = m_state[2], d = m_state[3];
        std::uint32_t e = m_state[4], f = m_state[5];
        std::uint32_t g = m_state[6], h = m_state[7];
        for (std::size_t i = 0; i < 64; ++i)
        {
            const std::uint32_t s1 =
                Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t t1 =
                h + s1 + choice + SHA256_K[i] + w[i];
            const std::uint32_t s0 =
                Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
            const std::uint32_t majority =
                (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + majority;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        m_state[0] += a; m_state[1] += b;
        m_state[2] += c; m_state[3] += d;
        m_state[4] += e; m_state[5] += f;
        m_state[6] += g; m_state[7] += h;
    }

    std::array<std::uint32_t, 8> m_state;
    std::array<std::uint8_t, 64> m_buffer;
    std::uint64_t m_bytes;
    std::size_t m_used;
};

const std::array<std::uint32_t, 256>& CrcTable()
{
    static const std::array<std::uint32_t, 256> table = []()
    {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t value = i;
            for (unsigned int bit = 0; bit < 8; ++bit)
                value = (value >> 1U) ^
                    ((value & 1U) ? 0x82f63b78U : 0U);
            values[i] = value;
        }
        return values;
    }();
    return table;
}

void SetError(std::string* error, const std::string& text)
{
    if (error != nullptr)
        *error = text;
}

} // namespace

Hash256::Hash256(): bytes() {}

std::string Hash256::ToHex() const
{
    static const char DIGITS[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (std::uint8_t byte : bytes)
    {
        result.push_back(DIGITS[byte >> 4U]);
        result.push_back(DIGITS[byte & 0xfU]);
    }
    return result;
}

bool Hash256::operator==(const Hash256& other) const
{
    return bytes == other.bytes;
}

bool Hash256::operator!=(const Hash256& other) const
{
    return !(*this == other);
}

bool Hash256::FromHex(const std::string& hex, Hash256& hash)
{
    if (hex.size() != 64)
        return false;
    const auto nibble = [](char value, std::uint8_t& output)
    {
        if (value >= '0' && value <= '9')
            output = static_cast<std::uint8_t>(value - '0');
        else if (value >= 'a' && value <= 'f')
            output = static_cast<std::uint8_t>(value - 'a' + 10);
        else
            return false;
        return true;
    };
    Hash256 parsed;
    for (std::size_t i = 0; i < parsed.bytes.size(); ++i)
    {
        std::uint8_t high = 0, low = 0;
        if (!nibble(hex[i * 2U], high) ||
            !nibble(hex[i * 2U + 1U], low))
            return false;
        parsed.bytes[i] = static_cast<std::uint8_t>((high << 4U) | low);
    }
    hash = parsed;
    return true;
}

std::uint32_t ComputeCrc32c(const void* bytes, std::size_t size)
{
    if (size != 0 && bytes == nullptr)
        return 0;
    const std::uint8_t* input =
        static_cast<const std::uint8_t*>(bytes);
    std::uint32_t crc = 0xffffffffU;
    const auto& table = CrcTable();
    for (std::size_t i = 0; i < size; ++i)
        crc = table[(crc ^ input[i]) & 0xffU] ^ (crc >> 8U);
    return ~crc;
}

Hash256 ComputeSha256(const void* bytes, std::size_t size)
{
    Sha256 builder;
    if (!builder.Update(bytes, size))
        return Hash256();
    return builder.Finish();
}

bool ComputeFileSha256(
    const std::filesystem::path& path,
    Hash256& hash,
    std::string* error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        SetError(error, "could not open file for SHA-256: " + path.string());
        return false;
    }
    Sha256 builder;
    std::array<char, 64 * 1024> buffer;
    while (input)
    {
        input.read(buffer.data(), buffer.size());
        const std::streamsize read = input.gcount();
        if (read > 0 &&
            !builder.Update(buffer.data(), static_cast<std::size_t>(read)))
        {
            SetError(error, "SHA-256 byte count overflow");
            return false;
        }
    }
    if (!input.eof())
    {
        SetError(error, "failed while reading file: " + path.string());
        return false;
    }
    hash = builder.Finish();
    return true;
}

bool IsValidEpisodeId(const std::string& episode_id)
{
    if (episode_id.size() != 32)
        return false;
    bool any_nonzero = false;
    for (char value : episode_id)
    {
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f')))
            return false;
        any_nonzero = any_nonzero || value != '0';
    }
    return any_nonzero;
}

const char* EpisodeStreamName(EpisodeStream stream)
{
    switch (stream)
    {
    case EpisodeStream::TELEMETRY: return "telemetry";
    case EpisodeStream::RGB: return "rgb";
    }
    return "unknown";
}

} // namespace WorldModel
} // namespace RoR

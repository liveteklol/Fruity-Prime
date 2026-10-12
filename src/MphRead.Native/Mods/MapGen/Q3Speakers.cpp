#include "Q3Speakers.hpp"

#include "CustomRooms.hpp"
#include "Q3Bsp.hpp"
#include "Q3Convert.hpp"
#include "../../Sound/Sfx.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/Number.hpp"
#include "../../NativeRuntime/System/ZipArchive.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::MapGen
{
    namespace
    {
        using OpenTK::Mathematics::Vector3;

        // Quake's falloff for a loop (snd_dma.c): full within 80 units, then
        // down by 0.0008 a unit -- silent from 1330.
        constexpr float FullVolume = 80.0F;
        constexpr float Silent = FullVolume + 1.0F / 0.0008F;
        constexpr float Volume = 0.7F;

        [[nodiscard]] const std::string* Value(const Q3Entity& entity, const std::string& key)
        {
            for (const auto& [name, value] : entity)
            {
                if (Q3StringEqual{}(name, key))
                {
                    return &value;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::string Lower(std::string text)
        {
            for (char& c : text)
            {
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : (c == '\\' ? '/' : c));
            }
            return text;
        }

        template <typename T>
        [[nodiscard]] T Read(const std::vector<std::uint8_t>& bytes, std::size_t at)
        {
            T value{};
            if (at + sizeof(T) <= bytes.size())
            {
                std::memcpy(&value, bytes.data() + at, sizeof(T));
            }
            return value;
        }

        // A RIFF WAVE of 8- or 16-bit PCM as 16-bit mono, and its rate.
        [[nodiscard]] std::optional<std::pair<std::vector<std::uint8_t>, std::int32_t>> Decode(
            const std::vector<std::uint8_t>& bytes)
        {
            if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0
                || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
            {
                return std::nullopt;
            }
            std::int32_t channels = 0;
            std::int32_t rate = 0;
            std::int32_t bits = 0;
            std::size_t dataAt = 0;
            std::size_t dataSize = 0;
            std::size_t at = 12;
            while (at + 8 <= bytes.size())
            {
                const std::uint32_t size = Read<std::uint32_t>(bytes, at + 4);
                if (std::memcmp(bytes.data() + at, "fmt ", 4) == 0 && Read<std::uint16_t>(bytes, at + 8) == 1)
                {
                    channels = Read<std::uint16_t>(bytes, at + 10);
                    rate = static_cast<std::int32_t>(Read<std::uint32_t>(bytes, at + 12));
                    bits = Read<std::uint16_t>(bytes, at + 22);
                }
                else if (std::memcmp(bytes.data() + at, "data", 4) == 0)
                {
                    dataAt = at + 8;
                    dataSize = std::min<std::size_t>(size, bytes.size() - dataAt);
                }
                at += 8 + size + (size & 1U);
            }
            if (channels <= 0 || rate <= 0 || (bits != 8 && bits != 16) || dataAt == 0)
            {
                return std::nullopt;
            }
            const std::size_t frame = static_cast<std::size_t>(channels) * static_cast<std::size_t>(bits / 8);
            const std::size_t frames = dataSize / frame;
            std::vector<std::uint8_t> pcm(frames * 2);
            for (std::size_t f = 0; f < frames; ++f)
            {
                std::int32_t sum = 0;
                for (std::int32_t c = 0; c < channels; ++c)
                {
                    const std::size_t sample = dataAt + f * frame + static_cast<std::size_t>(c) * static_cast<std::size_t>(bits / 8);
                    sum += bits == 8 ? (static_cast<std::int32_t>(bytes[sample]) - 128) * 256
                                     : static_cast<std::int32_t>(Read<std::int16_t>(bytes, sample));
                }
                const std::int16_t mono = static_cast<std::int16_t>(std::clamp(sum / channels, -32768, 32767));
                std::memcpy(pcm.data() + f * 2, &mono, 2);
            }
            return std::make_pair(std::move(pcm), rate);
        }

        struct Started
        {
            std::int32_t Room = -1;
            std::uint64_t Frame = 0;
        };
    }

    void Q3Speakers::Ensure(std::int32_t roomId, std::uint64_t frame)
    {
        static Started started;
        const bool again = started.Room == roomId && frame >= started.Frame;
        started.Frame = frame;
        if (again)
        {
            return;
        }
        started.Room = roomId;
        const std::shared_ptr<::MphRead::Sound::SfxInstanceBase> sfx = ::MphRead::Sound::Sfx::Instance();
        if (sfx == nullptr)
        {
            return;
        }
        sfx->StopAmbientLoops();
        const std::shared_ptr<Q3Bsp> bsp = CustomRooms::Q3LevelBsp(roomId);
        if (bsp == nullptr)
        {
            return;
        }
        std::vector<std::shared_ptr<::MphRead::NativeRuntime::ZipArchive>> archives;
        for (const std::string& path : CustomRooms::Q3LevelArchives(roomId))
        {
            try
            {
                archives.push_back(::MphRead::NativeRuntime::ZipArchive::OpenRead(path));
            }
            catch (...)
            {
            }
        }
        const float unit = Q3Convert::UnitsPerUnit;
        std::int32_t playing = 0;
        for (const std::shared_ptr<Q3Entity>& entity : bsp->Entities())
        {
            const std::string* kind = entity == nullptr ? nullptr : Value(*entity, "classname");
            const std::string* noise = kind == nullptr || Lower(*kind) != "target_speaker" ? nullptr : Value(*entity, "noise");
            const std::string* origin = noise == nullptr ? nullptr : Value(*entity, "origin");
            if (origin == nullptr || noise->empty() || (*noise)[0] == '*')
            {
                continue;
            }
            float flags = 0.0F;
            if (const std::string* text = Value(*entity, "spawnflags"))
            {
                (void)::MphRead::NativeRuntime::SingleTryParseInvariant(*text, flags);
            }
            const std::int32_t spawnflags = static_cast<std::int32_t>(flags);
            if ((spawnflags & 1) == 0)
            {
                continue;
            }
            std::array<float, 3> at{};
            std::size_t found = 0;
            std::size_t start = 0;
            while (found < 3 && start < origin->size())
            {
                const std::size_t end = std::min(origin->find(' ', start), origin->size());
                if (end > start && ::MphRead::NativeRuntime::SingleTryParseInvariant(
                        std::string_view(*origin).substr(start, end - start), at[found]))
                {
                    ++found;
                }
                start = end + 1;
            }
            if (found < 3)
            {
                continue;
            }
            std::string path = Lower(*noise);
            if (path.rfind("sound/", 0) != 0)
            {
                path = "sound/" + path;
            }
            std::optional<std::vector<std::uint8_t>> bytes;
            for (const auto& archive : archives)
            {
                for (std::size_t i = 0; archive != nullptr && i < archive->Count() && !bytes; ++i)
                {
                    if (Lower(archive->FullName(i)) == path)
                    {
                        bytes = archive->Read(i);
                    }
                }
                if (bytes)
                {
                    break;
                }
            }
            if (!bytes)
            {
                continue;
            }
            if (const auto decoded = Decode(*bytes))
            {
                // A global speaker (4) is heard everywhere at once.
                const bool global = (spawnflags & 4) != 0;
                const Vector3 position(at[0] / unit, at[2] / unit, -at[1] / unit);
                sfx->PlayAmbientLoop(decoded->first, decoded->second, position, Volume,
                    global ? std::numeric_limits<float>::max() : FullVolume / unit,
                    global ? std::numeric_limits<float>::max() : Silent / unit);
                ++playing;
            }
        }
        ::MphRead::NativeRuntime::ConsoleWriteLine("[q3] " + std::to_string(playing) + " ambient loops");
    }
}

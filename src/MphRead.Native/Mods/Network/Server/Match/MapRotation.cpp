#include "Mods/Network/Server/Match/MapRotation.hpp"
#include "Mods/Branding.hpp"

#include "Formats/Formats.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/IO.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    std::vector<std::string> SplitOnPipe(std::string_view value)
    {
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (true)
        {
            const std::size_t separator = value.find('|', start);
            if (separator == std::string_view::npos)
            {
                parts.emplace_back(value.substr(start));
                break;
            }
            parts.emplace_back(value.substr(start, separator - start));
            start = separator + 1;
        }
        return parts;
    }

    void RemoveAsciiSpaces(std::string& value)
    {
        value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
    }
}

namespace MphRead::Mods::Network
{
    RotationEntry::RotationEntry()
        : RoomKey("MP3 PROVING GROUND"),
          Mode(GameMode::Battle),
          TimeLimit(7.0f * 60.0f),
          PointGoal(7)
    {
    }

    RotationEntry::RotationEntry(std::string roomKey, GameMode mode, float timeLimit,
        std::int32_t pointGoal)
        : RoomKey(std::move(roomKey)),
          Mode(mode),
          TimeLimit(timeLimit),
          PointGoal(pointGoal)
    {
    }

    std::string RotationEntry::ToString() const
    {
        std::string value = RoomKey;
        value += " (";
        value += ::MphRead::ToString(Mode);
        value += ", ";
        value += ::MphRead::NativeRuntime::ToString(TimeLimit / 60.0f, "0.#");
        value += " min, ";
        value += ::MphRead::NativeRuntime::ToString(PointGoal);
        value += " pts)";
        return value;
    }

    const std::shared_ptr<const RotationEntry> MapRotation::_fallback(
        new RotationEntry());

    const std::vector<std::shared_ptr<const RotationEntry>>& MapRotation::Entries() const
    {
        return _entries;
    }

    std::shared_ptr<const RotationEntry> MapRotation::Current() const
    {
        if (_override != nullptr)
        {
            return _override;
        }
        if (!_entries.empty())
        {
            return _entries[static_cast<std::size_t>(_index)];
        }
        return _fallback;
    }

    std::shared_ptr<const RotationEntry> MapRotation::Next() const
    {
        if (_pending != nullptr)
        {
            return _pending;
        }
        if (!_entries.empty())
        {
            const std::int32_t count = static_cast<std::int32_t>(_entries.size());
            const std::int32_t next = (_index + 1) % count;
            return _entries[static_cast<std::size_t>(next)];
        }
        return _fallback;
    }

    std::int32_t MapRotation::Index() const
    {
        return _index;
    }

    std::shared_ptr<MapRotation> MapRotation::SingleMatch(const std::string& roomKey,
        GameMode mode, float timeLimit, std::int32_t pointGoal)
    {
        auto rotation = std::make_shared<MapRotation>();
        const GameMode actualMode = mode == GameMode::None
            ? GameMode::Battle
            : mode;
        rotation->_entries.emplace_back(new RotationEntry(
            roomKey, actualMode, timeLimit, pointGoal));
        return rotation;
    }

    std::shared_ptr<MapRotation> MapRotation::FromList(
        const std::vector<std::pair<std::string, GameMode>>& maps, float timeLimit, std::int32_t pointGoal)
    {
        auto rotation = std::make_shared<MapRotation>();
        for (const auto& [room, mode] : maps)
        {
            if (room.empty())
            {
                continue;
            }
            rotation->_entries.emplace_back(new RotationEntry(
                room, mode == GameMode::None ? GameMode::Battle : mode, timeLimit, pointGoal));
        }
        if (rotation->_entries.empty())
        {
            rotation->_entries.push_back(_fallback);
        }
        return rotation;
    }

    void MapRotation::WriteList(const std::string& path,
        const std::vector<std::pair<std::string, GameMode>>& maps, float timeLimit, std::int32_t pointGoal)
    {
        std::vector<std::string> lines{
            "# Written by " + std::string(Mods::Branding::Name) + "'s create-server screen.",
            "# One match per line:  ROOM KEY | mode | minutes | points",
            ""
        };
        for (const auto& [room, mode] : maps)
        {
            if (room.empty())
            {
                continue;
            }
            lines.push_back(room + " | " + ::MphRead::ToString(mode == GameMode::None ? GameMode::Battle : mode)
                + " | " + NativeRuntime::ToStringInvariant(timeLimit / 60, "0.#")
                + " | " + std::to_string(pointGoal));
        }
        NativeRuntime::FileWriteAllLines(path, lines);
    }

    void MapRotation::ClearPending()
    {
        _pending = nullptr;
    }

    void MapRotation::PlayNext(const std::string& roomKey, GameMode mode)
    {
        const std::shared_ptr<const RotationEntry> current = Current();
        const GameMode actualMode = mode == GameMode::None
            ? current->Mode
            : mode;
        _pending = std::shared_ptr<const RotationEntry>(new RotationEntry(
            roomKey, actualMode, current->TimeLimit, current->PointGoal));
    }

    std::shared_ptr<const RotationEntry> MapRotation::Advance()
    {
        if (_pending != nullptr)
        {
            _override = _pending;
            _pending = nullptr;
            return Current();
        }
        if (_override != nullptr)
        {
            _override = nullptr;
        }
        if (!_entries.empty())
        {
            const std::int32_t count = static_cast<std::int32_t>(_entries.size());
            _index = (_index + 1) % count;
        }
        return Current();
    }

    std::shared_ptr<MapRotation> MapRotation::Load(const std::string& path)
    {
        auto rotation = std::make_shared<MapRotation>();
        for (const std::string& raw : NativeRuntime::FileReadAllLines(std::string(path)))
        {
            std::string line = raw;
            const std::size_t comment = line.find('#');
            if (comment != std::string::npos)
            {
                line = line.substr(0, comment);
            }
            line = NativeRuntime::StringTrim(std::string(line));
            if (line.empty())
            {
                continue;
            }

            const std::vector<std::string> parts = SplitOnPipe(line);
            const std::string roomKey = NativeRuntime::StringTrim(std::string(parts[0]));
            if (roomKey.empty())
            {
                continue;
            }

            GameMode mode = GameMode::Battle;
            if (parts.size() > 1)
            {
                std::string modeText = NativeRuntime::StringTrim(std::string(parts[1]));
                RemoveAsciiSpaces(modeText);
                GameMode parsedMode = mode;
                if (::MphRead::TryParse(modeText, true, parsedMode))
                {
                    mode = parsedMode;
                }
            }

            float timeLimit = 7.0f * 60.0f;
            if (parts.size() > 2)
            {
                const std::string minutesText = NativeRuntime::StringTrim(std::string(parts[2]));
                float minutes = 0.0f;
                if (::MphRead::NativeRuntime::SingleTryParseInvariant(std::string(minutesText), minutes))
                {
                    timeLimit = minutes * 60.0f;
                }
            }

            std::int32_t pointGoal = 7;
            if (parts.size() > 3)
            {
                const std::string pointsText = NativeRuntime::StringTrim(std::string(parts[3]));
                std::int32_t parsedPoints = 0;
                if (NativeRuntime::Int32TryParseCurrentCulture(pointsText, parsedPoints))
                {
                    pointGoal = parsedPoints;
                }
            }

            rotation->_entries.emplace_back(new RotationEntry(
                roomKey, mode, timeLimit, pointGoal));
        }
        return rotation;
    }

    void MapRotation::WriteDefault(const std::string& path)
    {
        NativeRuntime::FileWriteAllLines(std::string(path),
        {
            "# MphRead dedicated server map rotation.",
            "# One match per line:  ROOM KEY | mode | minutes | points",
            "# Mode and the numbers are optional; '#' starts a comment.",
            "# Room keys are the names MphRead uses internally -- run the",
            "# game's console menu to see the full list.",
            "",
            "MP1 SANCTORUS      | Battle | 7 | 7",
            "MP3 PROVING GROUND | Battle | 7 | 7",
            "MP4 HIGHGROUND     | Battle | 7 | 7",
            "MP2 HARVESTER      | Battle | 7 | 7",
            "MP6 HEADSHOT       | Battle | 7 | 7"
        });
    }

    std::shared_ptr<MapRotation> MapRotation::LoadOrCreate(const std::string& path)
    {
        if (!NativeRuntime::FileExists(std::string(path)))
        {
            WriteDefault(path);
            NativeRuntime::ConsoleWriteLine(
                std::string("[server] wrote a starter rotation to ") + path);
        }
        std::shared_ptr<MapRotation> rotation = Load(path);
        if (rotation->_entries.empty())
        {
            NativeRuntime::ConsoleWriteLine(
                std::string("[server] ") + path
                + " has no usable entries; using a single default map");
            rotation->_entries.push_back(_fallback);
        }
        return rotation;
    }
}

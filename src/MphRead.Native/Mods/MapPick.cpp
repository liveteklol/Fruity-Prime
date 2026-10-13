#include "MapPick.hpp"

#include "ThumbnailGenerator.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "../Metadata/Metadata.hpp"
#include "../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <exception>

namespace MphRead::Mods
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::vector<std::string> MapPick::_order{};
    MapPick::IgnoreCaseMap MapPick::_tally{};
    std::vector<std::string> MapPick::_voted{};
    std::vector<std::string> MapPick::_rooms{};
    bool MapPick::_open = false;
    std::int32_t MapPick::_eligible = 0;
    std::string MapPick::_picked{};
    std::int32_t MapPick::_cursor = 0;
    std::int32_t MapPick::_scroll = 0;
    std::int32_t MapPick::_window = 4;
    std::map<std::string, std::string, NativeRuntime::OrdinalIgnoreCaseLess> MapPick::_labels{};
    std::vector<EndScreen::Hit> MapPick::_hits{};

    bool MapPick::Available()
    {
        return _open && !_order.empty() && EndScreen::Available();
    }

    std::int32_t MapPick::VotesFor(const std::string& roomKey)
    {
        const auto found = _tally.find(roomKey);
        return found != _tally.end() ? found->second : 0;
    }

    std::string MapPick::Leader()
    {
        return !_order.empty() && VotesFor(_order[0]) > 0 ? _order[0] : std::string();
    }

    std::string MapPick::NameOf(const std::string& roomKey)
    {
        if (roomKey.empty())
        {
            return "";
        }
        const auto found = _labels.find(roomKey);
        if (found != _labels.end())
        {
            return found->second;
        }
        return PlainName(roomKey);
    }

    std::string MapPick::PlainName(const std::string& roomKey)
    {
        try
        {
            auto [meta, ignored] = Metadata::GetRoomByName(roomKey);
            static_cast<void>(ignored);
            return meta != nullptr && meta->InGameName.has_value() ? *meta->InGameName : roomKey;
        }
        catch (const std::exception&)
        {
            return roomKey;
        }
    }

    bool MapPick::Contains(const std::vector<std::string>& list, const std::string& value)
    {
        return std::any_of(list.begin(), list.end(),
            [&value](const std::string& entry) { return Runtime::StringEqualsOrdinalIgnoreCase(entry, value); });
    }

    void MapPick::BuildLabels()
    {
        _labels.clear();
        std::map<std::string, std::int32_t, NativeRuntime::OrdinalIgnoreCaseLess> seen;
        for (const std::string& room : _rooms)
        {
            const std::string name = PlainName(room);
            const auto found = seen.find(name);
            seen[name] = found != seen.end() ? found->second + 1 : 1;
        }
        for (const std::string& key : _rooms)
        {
            const std::string name = PlainName(key);
            if (seen[name] <= 1 || Runtime::StringEqualsOrdinalIgnoreCase(name, key))
            {
                _labels[key] = name;
                continue;
            }
            std::string extra = key;
            for (const std::string& word : Runtime::StringSplit(name, ' '))
            {
                extra = Runtime::StringReplaceOrdinalIgnoreCase(extra, word, "");
            }
            const std::vector<std::string> words = Runtime::StringSplit(extra, ' ', true, true);
            extra.clear();
            for (std::size_t i = 0; i < words.size(); i++)
            {
                if (i > 0)
                {
                    extra += ' ';
                }
                extra += words[i];
            }
            _labels[key] = !extra.empty() ? name + " " + extra : name;
        }
    }

    void MapPick::Begin(const std::string& currentRoom, bool open)
    {
        _rooms.clear();
        try
        {
            const std::vector<std::string> rooms = ThumbnailGenerator::MultiplayerRooms();
            for (const std::string& room : rooms)
            {
                if (!Runtime::StringEqualsOrdinalIgnoreCase(room, currentRoom))
                {
                    _rooms.push_back(room);
                }
            }
        }
        catch (const std::exception&)
        {
        }
        BuildLabels();
        _open = open && !_rooms.empty();
        _cursor = 0;
        _scroll = 0;
        Reorder();
    }

    void MapPick::Reorder()
    {
        _order.clear();
        for (const std::string& voted : _voted)
        {
            if (Contains(_rooms, voted))
            {
                _order.push_back(voted);
            }
        }
        for (const std::string& room : _rooms)
        {
            if (!Contains(_order, room))
            {
                _order.push_back(room);
            }
        }
        _cursor = std::clamp(_cursor, 0, std::max(0, static_cast<std::int32_t>(_order.size()) - 1));
        ClampScroll();
    }

    void MapPick::Apply(const Network::MapChoicesPacket& packet)
    {
        _eligible = packet.Eligible;
        const bool open = packet.Open != 0;
        _tally.clear();
        _voted.clear();
        const auto& keys = Runtime::RequireReference(packet.RoomKeys);
        const auto& votes = Runtime::RequireReference(packet.Votes);
        for (std::int32_t i = 0; i < packet.Count && i < static_cast<std::int32_t>(keys.size()); i++)
        {
            const std::string key = keys[static_cast<std::size_t>(i)].value_or("");
            if (key.empty())
            {
                continue;
            }
            _tally[key] = i < static_cast<std::int32_t>(votes.size()) ? votes[static_cast<std::size_t>(i)] : 0;
            _voted.push_back(key);
        }
        if (!open)
        {
            _open = false;
            _picked.clear();
            _order.clear();
            return;
        }
        if (!_open && !_rooms.empty())
        {
            _open = true;
        }
        const std::string was = _cursor >= 0 && _cursor < static_cast<std::int32_t>(_order.size())
            ? _order[static_cast<std::size_t>(_cursor)] : std::string();
        Reorder();
        if (!was.empty())
        {
            const std::int32_t at = IndexOf(was);
            if (at >= 0)
            {
                _cursor = at;
            }
        }
    }

    std::int32_t MapPick::IndexOf(const std::string& roomKey)
    {
        for (std::size_t i = 0; i < _order.size(); i++)
        {
            if (Runtime::StringEqualsOrdinalIgnoreCase(_order[i], roomKey))
            {
                return static_cast<std::int32_t>(i);
            }
        }
        return -1;
    }

    std::int32_t MapPick::PickedIndex()
    {
        return _picked.empty() ? -1 : IndexOf(_picked);
    }

    void MapPick::Reset()
    {
        _order.clear();
        _rooms.clear();
        _tally.clear();
        _voted.clear();
        _labels.clear();
        _picked.clear();
        _open = false;
        _cursor = 0;
        _scroll = 0;
        _eligible = 0;
        _hits.clear();
    }

    std::string MapPick::Chosen()
    {
        return _picked;
    }

    void MapPick::Choose(std::int32_t index)
    {
        if (index < 0 || index >= static_cast<std::int32_t>(_order.size()))
        {
            return;
        }
        _cursor = index;
        ScrollToCursor();
        const std::string key = _order[static_cast<std::size_t>(index)];
        _picked = Runtime::StringEqualsOrdinalIgnoreCase(_picked, key) ? std::string() : key;
        if (Network::NetSession::Active())
        {
            Network::NetSession::SendMapPick(_picked);
            return;
        }
        _tally.clear();
        _voted.clear();
        if (!_picked.empty())
        {
            _tally[_picked] = 1;
            _voted.push_back(_picked);
        }
        const std::string was = _cursor < static_cast<std::int32_t>(_order.size())
            ? _order[static_cast<std::size_t>(_cursor)] : std::string();
        Reorder();
        const std::int32_t at = IndexOf(was);
        if (at >= 0)
        {
            _cursor = at;
            ScrollToCursor();
        }
    }

    void MapPick::ChooseCursor()
    {
        Choose(_cursor);
    }

    void MapPick::Step(std::int32_t by)
    {
        if (_order.empty())
        {
            return;
        }
        _cursor = std::clamp(_cursor + by, 0, static_cast<std::int32_t>(_order.size()) - 1);
        ScrollToCursor();
    }

    void MapPick::Wheel(std::int32_t by)
    {
        if (_order.empty())
        {
            return;
        }
        _scroll = std::clamp(_scroll + by, 0, std::max(0, static_cast<std::int32_t>(_order.size()) - _window));
    }

    void MapPick::ClampScroll()
    {
        _scroll = std::clamp(_scroll, 0, std::max(0, static_cast<std::int32_t>(_order.size()) - std::max(1, _window)));
    }

    void MapPick::ScrollToCursor()
    {
        const std::int32_t window = std::max(1, _window);
        if (_cursor < _scroll)
        {
            _scroll = _cursor;
        }
        else if (_cursor >= _scroll + window)
        {
            _scroll = _cursor - window + 1;
        }
        _scroll = std::clamp(_scroll, 0, std::max(0, static_cast<std::int32_t>(_order.size()) - window));
    }

    void MapPick::Resend()
    {
        if (!_picked.empty() && Network::NetSession::Active())
        {
            Network::NetSession::SendMapPick(_picked);
        }
    }

    void MapPick::NoteLayout(const std::vector<EndScreen::Hit>& rows, std::int32_t window)
    {
        std::vector<EndScreen::Hit>(rows).swap(_hits);
        if (window > 0 && window != _window)
        {
            _window = window;
            ScrollToCursor();
        }
    }

    std::int32_t MapPick::Hovered()
    {
        if (!Available())
        {
            return -1;
        }
        for (std::size_t i = 0; i < _hits.size(); i++)
        {
            if (_hits[i].Contains(EndScreen::PointerX(), EndScreen::PointerY()))
            {
                return _scroll + static_cast<std::int32_t>(i);
            }
        }
        return -1;
    }

    bool MapPick::HandleClick()
    {
        const std::int32_t row = Hovered();
        if (row < 0)
        {
            return false;
        }
        Choose(row);
        return true;
    }
}

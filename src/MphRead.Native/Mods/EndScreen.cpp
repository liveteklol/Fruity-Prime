#include "EndScreen.hpp"

#include "MapPick.hpp"
#include "Input/GamepadManager.hpp"
#include "Input/GamepadUiRouter.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Render/MapThumbnail.hpp"
#include "../NativeRuntime/System/Runtime.hpp"
#include "../Entities/Players/PlayerEntity.hpp"
#include "../Entities/Players/PlayerInput.hpp"
#include "../Formats/Types.hpp"
#include "../GameState.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Metadata/Rooms.hpp"
#include "Input/GamepadInput.hpp"
#include "Launcher/Portable/LaunchPlan.hpp"
#include "Launcher/Portable/LauncherPrefs.hpp"
#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Gameplay/Player/PlayerColors.hpp"
#include "RespawnChoice.hpp"
#include "SpectatorMode.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void ReplaceHit(
        MphRead::Mods::EndScreen::Hit& target,
        const MphRead::Mods::EndScreen::Hit& source)
    {
        target.~Hit();
        ::new (static_cast<void*>(std::addressof(target)))
            MphRead::Mods::EndScreen::Hit(source);
    }
}

namespace MphRead::Mods
{
    bool EndScreen::_ready = false;
    EndScreen::Hit EndScreen::_hitPrev{};
    EndScreen::Hit EndScreen::_hitNext{};
    EndScreen::Hit EndScreen::_hitReady{};
    std::vector<EndScreen::Hit> EndScreen::_hitSuits(
        static_cast<std::size_t>(Network::PlayerColors::Count));
    float EndScreen::_pointerX = -1.0F;
    float EndScreen::_pointerY = -1.0F;

    bool EndScreen::Hit::Contains(float x, float y) const
    {
        return Right > Left && Bottom > Top
            && x >= Left && x < Right && y >= Top && y < Bottom;
    }

    bool EndScreen::Available()
    {
        if (!MphRead::GameState::Multiplayer() || MphRead::GameState::MenuPause()
            || Network::DemoPlayback::IsActive() || SpectatorMode::IsSpectating()
            || SpectatorMode::FreeCamera() || Entities::PlayerEntity::Main() == nullptr)
        {
            return false;
        }
        return MphRead::GameState::MatchState() == MphRead::MatchState::GameOver
            || MphRead::GameState::MatchState() == MphRead::MatchState::Ending;
    }

    bool EndScreen::Ready()
    {
        return _ready;
    }

    void EndScreen::ClearReady()
    {
        _ready = false;
    }

    void EndScreen::ToggleReady()
    {
        if (Available())
        {
            _ready = !_ready;
        }
    }

    MphRead::Hunter EndScreen::Hunter()
    {
        return RespawnChoice::Hunter();
    }

    std::int32_t EndScreen::Suit()
    {
        return Network::PlayerColors::Clamp(RespawnChoice::Color());
    }

    std::string EndScreen::NextRoomKey()
    {
        std::optional<Network::MatchStatePacket> state = Network::NetSession::ServerMatch();
        if (!state.has_value())
        {
            return "";
        }
        return state->NextRoomKey.value_or("");
    }

    std::string EndScreen::NextRoomName()
    {
        std::string key = NextRoomKey();
        if (key.empty())
        {
            return "";
        }
        try
        {
            auto [meta, ignored] = MphRead::Metadata::GetRoomByName(key);
            (void)ignored;
            if (meta != nullptr && meta->InGameName.has_value())
            {
                return meta->InGameName.value();
            }
            return key;
        }
        catch (const std::exception&)
        {
            return key;
        }
    }

    float EndScreen::PointerX()
    {
        return _pointerX;
    }

    float EndScreen::PointerY()
    {
        return _pointerY;
    }

    void EndScreen::NotePointer(float x, float y)
    {
        _pointerX = x;
        _pointerY = y;
    }

    void EndScreen::NoteLayout(
        Hit previous,
        Hit next,
        std::shared_ptr<std::vector<Hit>> suits,
        Hit ready)
    {
        ReplaceHit(_hitPrev, previous);
        ReplaceHit(_hitNext, next);
        ReplaceHit(_hitReady, ready);

        if (suits == nullptr)
        {
            throw System::NullReferenceException();
        }
        for (std::size_t i = 0; i < _hitSuits.size() && i < suits->size(); ++i)
        {
            ReplaceHit(_hitSuits[i], (*suits)[i]);
        }
    }

    void EndScreen::Tick(const std::string& roomKey, double time)
    {
        const bool up = Available();
        if (up == _wasUp)
        {
            if (up && time - _resentAt >= 1)
            {
                _resentAt = time;
                MapPick::Resend();
            }
            return;
        }
        _wasUp = up;
        _resentAt = time;
        // The previews are textures in the scene's own counted names, and the
        // counter restarts with every room: nothing may survive a map change.
        Render::MapThumbnail::Clear();
        if (!up)
        {
            MapPick::Reset();
            return;
        }
        // Open straight away offline; online it waits for the server.
        MapPick::Begin(roomKey, !Network::NetSession::Active());
    }

    std::int32_t EndScreen::HoveredSuit()
    {
        if (!Available())
        {
            return -1;
        }
        for (std::size_t i = 0; i < _hitSuits.size(); ++i)
        {
            if (_hitSuits[i].Contains(PointerX(), PointerY()))
            {
                return static_cast<std::int32_t>(i);
            }
        }
        return -1;
    }

    bool EndScreen::HoveredPrev()
    {
        return Available() && _hitPrev.Contains(PointerX(), PointerY());
    }

    bool EndScreen::HoveredNext()
    {
        return Available() && _hitNext.Contains(PointerX(), PointerY());
    }

    bool EndScreen::HoveredReady()
    {
        return Available() && _hitReady.Contains(PointerX(), PointerY());
    }

    bool EndScreen::HandleClick()
    {
        if (!Available())
        {
            return false;
        }
        if (_hitPrev.Contains(PointerX(), PointerY()))
        {
            Step(-1, 0);
            return true;
        }
        if (_hitNext.Contains(PointerX(), PointerY()))
        {
            Step(1, 0);
            return true;
        }
        if (_hitReady.Contains(PointerX(), PointerY()))
        {
            ToggleReady();
            return true;
        }
        for (std::size_t i = 0; i < _hitSuits.size(); ++i)
        {
            if (_hitSuits[i].Contains(PointerX(), PointerY()))
            {
                Choose(Hunter(), static_cast<std::int32_t>(i));
                return true;
            }
        }
        // The ballot under the picker. Last only because it is the cheapest
        // test to reach; the two layouts do not overlap.
        return MapPick::HandleClick();
    }

    bool EndScreen::HandleKeyDown(
        OpenTK::Windowing::GraphicsLibraryFramework::Keys key)
    {
        using OpenTK::Windowing::GraphicsLibraryFramework::Keys;

        if (!Available())
        {
            return false;
        }
        switch (key)
        {
        case Keys::Left:
            Step(-1, 0);
            return true;
        case Keys::Right:
            Step(1, 0);
            return true;
        case Keys::Up:
            StepList(-1);
            return true;
        case Keys::Down:
            StepList(1);
            return true;
        case Keys::Space:
            if (MapPick::Available())
            {
                MapPick::ChooseCursor();
                return true;
            }
            return false;
        default:
            break;
        }
        if (key == static_cast<Keys>(257) || key == static_cast<Keys>(335))
        {
            ToggleReady();
            return true;
        }
        return false;
    }

    Input::GamepadUiRouter& EndScreen::ResultPad()
    {
        static Input::GamepadUiRouter& router = []() -> Input::GamepadUiRouter&
        {
            static Input::GamepadUiRouter created{};
            static_cast<void>(created.Action.Add([](Input::UiAction action)
            {
                switch (action)
                {
                case Input::UiAction::Left: Step(-1, 0); break;
                case Input::UiAction::Right: Step(1, 0); break;
                case Input::UiAction::Up: StepList(-1); break;
                case Input::UiAction::Down: StepList(1); break;
                case Input::UiAction::Accept: ToggleReady(); break;
                case Input::UiAction::NextTab:
                    if (MapPick::Available())
                    {
                        MapPick::ChooseCursor();
                    }
                    break;
                default: break;
                }
            }));
            return created;
        }();
        return router;
    }

    void EndScreen::PollGamepad()
    {
        ResultPad().Update(Input::GamepadManager::Snapshot(), Available() && Input::GamepadContexts::Focused()
            && Input::GamepadContexts::Current() == Input::GamepadContext::Results
            ? Input::GamepadContext::Results : Input::GamepadContext::Gameplay,
            ::MphRead::NativeRuntime::EnvironmentTickCount64());
    }

    // Up and down: the map ballot while there is one, and the suit otherwise.
    void EndScreen::StepList(std::int32_t by)
    {
        if (MapPick::Available())
        {
            MapPick::Step(by);
            return;
        }
        Step(0, by);
    }

    void EndScreen::Step(std::int32_t hunterBy, std::int32_t suitBy)
    {
        std::int32_t hunter = static_cast<std::int32_t>(Launcher::Hunters::Resolve(Hunter()));
        if (hunterBy != 0)
        {
            hunter = ((hunter + hunterBy) % Launcher::Hunters::Playable
                + Launcher::Hunters::Playable) % Launcher::Hunters::Playable;
        }
        std::int32_t suit = Suit();
        if (suitBy != 0)
        {
            suit = ((suit + suitBy) % Network::PlayerColors::Count
                + Network::PlayerColors::Count) % Network::PlayerColors::Count;
        }
        Choose(static_cast<MphRead::Hunter>(hunter), suit);
    }

    void EndScreen::Pick(MphRead::Hunter hunter, std::int32_t suit)
    {
        Choose(hunter, suit);
    }

    void EndScreen::Choose(MphRead::Hunter hunter, std::int32_t suit)
    {
        RespawnChoice::Request(hunter, suit);
        Launcher::LauncherPrefs::LastHunter(hunter);
        Launcher::LauncherPrefs::LastColor(suit);
        Launcher::LauncherPrefs::Save();
    }
}

namespace MphRead::Mods::Detail
{
    void RespawnChoiceEndScreenClearReady()
    {
        EndScreen::ClearReady();
    }
}

namespace MphRead::Mods::Network::Detail
{
    bool NetPlayerBridgeEndScreenReady()
    {
        return MphRead::Mods::EndScreen::Ready();
    }
}

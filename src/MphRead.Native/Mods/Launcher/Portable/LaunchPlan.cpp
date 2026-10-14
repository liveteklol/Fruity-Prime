#include "LaunchPlan.hpp"
#include "../../../NativeRuntime/System/Random.hpp"

#include <random>

namespace
{
    constexpr MphRead::Hunter RandomHunter = static_cast<MphRead::Hunter>(8);

    MphRead::Hunter RollPlayableHunter()
    {
        return static_cast<MphRead::Hunter>(
            MphRead::NativeRuntime::RandomSharedNext(MphRead::Mods::Launcher::Hunters::Playable));
    }
}

namespace MphRead::Mods::Launcher
{
    std::atomic<MphRead::Hunter> Hunters::_rolled{RandomHunter};

    MphRead::Hunter Hunters::Resolve(MphRead::Hunter hunter)
    {
        if (hunter != RandomHunter)
        {
            return hunter;
        }
        if (_rolled.load(std::memory_order_relaxed) == RandomHunter)
        {
            _rolled.store(RollPlayableHunter(), std::memory_order_relaxed);
        }
        return _rolled.load(std::memory_order_relaxed);
    }

    void Hunters::Reroll() noexcept
    {
        _rolled.store(RandomHunter, std::memory_order_relaxed);
    }

    LaunchPlan::HunterInit::HunterInit(MphRead::Hunter value)
        : _value(Hunters::Resolve(value))
    {
    }

    LaunchPlan::HunterInit& LaunchPlan::HunterInit::operator=(MphRead::Hunter value)
    {
        _value = Hunters::Resolve(value);
        return *this;
    }

    LaunchPlan::HunterInit::operator MphRead::Hunter() const noexcept
    {
        return _value;
    }

    LaunchPlan::LaunchPlan(const Init& init)
        : _kind(init.Kind),
          _lobby(init.Lobby),
          _hunter(init.Hunter),
          _roomKey(init.RoomKey),
          _mode(init.Mode),
          _bots(init.Bots),
          _botLevel(init.BotLevel),
          _instagib(init.Instagib),
          _port(init.Port),
          _playerName(init.PlayerName),
          _saveSlot(init.SaveSlot),
          _newGame(init.NewGame),
          _demoPath(init.DemoPath)
    {
    }

    LaunchPlan::LaunchPlan(LaunchPlan&& other)
        : LaunchPlan(static_cast<const LaunchPlan&>(other))
    {
    }

    LaunchPlan& LaunchPlan::operator=(LaunchPlan&& other)
    {
        return operator=(static_cast<const LaunchPlan&>(other));
    }

    LaunchKind LaunchPlan::Kind() const noexcept
    {
        return _kind;
    }

    const std::shared_ptr<LobbyContext>& LaunchPlan::Lobby() const noexcept
    {
        return _lobby;
    }

    MphRead::Hunter LaunchPlan::Hunter() const noexcept
    {
        return _hunter;
    }

    const std::optional<std::string>& LaunchPlan::RoomKey() const noexcept
    {
        return _roomKey;
    }

    MphRead::GameMode LaunchPlan::Mode() const noexcept
    {
        return _mode;
    }

    std::int32_t LaunchPlan::Bots() const noexcept
    {
        return _bots;
    }

    std::int32_t LaunchPlan::BotLevel() const noexcept
    {
        return _botLevel;
    }

    bool LaunchPlan::Instagib() const noexcept
    {
        return _instagib;
    }

    std::int32_t LaunchPlan::Port() const noexcept
    {
        return _port;
    }

    const std::optional<std::string>& LaunchPlan::PlayerName() const noexcept
    {
        return _playerName;
    }

    std::uint8_t LaunchPlan::SaveSlot() const noexcept
    {
        return _saveSlot;
    }

    bool LaunchPlan::NewGame() const noexcept
    {
        return _newGame;
    }

    const std::optional<std::string>& LaunchPlan::DemoPath() const noexcept
    {
        return _demoPath;
    }
}

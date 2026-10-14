#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace MphRead
{
    enum class Hunter : std::uint8_t;
    enum class GameMode : std::uint8_t;
}

namespace MphRead::Mods::Launcher
{
    class Hunters final
    {
    public:
        static constexpr std::int32_t Playable = 7;

        Hunters() = delete;

        [[nodiscard]] static MphRead::Hunter Resolve(MphRead::Hunter hunter);
        static void Reroll() noexcept;

    private:
        static std::atomic<MphRead::Hunter> _rolled;
    };

    // Client-only identity carried through the persistent lobby; never serialized.
    class LobbyContext final
    {
    public:
        LobbyContext(std::string serverName, std::string endpoint, bool createdLocally = false)
            : ServerName(std::move(serverName)), Endpoint(std::move(endpoint)), CreatedLocally(createdLocally)
        {
        }

        const std::string ServerName;
        const std::string Endpoint;
        const bool CreatedLocally;

        [[nodiscard]] bool operator==(const LobbyContext&) const = default;
    };

    enum class LaunchKind : std::int32_t
    {
        None = 0,
        Online = 1,
        Offline = 2,
        Host = 3,
        Adventure = 4,
        Demo = 5
    };

    class LaunchPlan final
    {
    private:
        class HunterInit final
        {
        public:
            HunterInit() noexcept = default;
            HunterInit(MphRead::Hunter value);
            HunterInit& operator=(MphRead::Hunter value);
            operator MphRead::Hunter() const noexcept;

        private:
            MphRead::Hunter _value = static_cast<MphRead::Hunter>(0);
        };

    public:
        struct Init
        {
            LaunchKind Kind = LaunchKind::None;
            std::shared_ptr<LobbyContext> Lobby{};
            HunterInit Hunter{};
            std::optional<std::string> RoomKey{};
            MphRead::GameMode Mode = static_cast<MphRead::GameMode>(0);
            std::int32_t Bots = 0;
            std::int32_t BotLevel = 0;
            // The Instagib rule, for an offline match. A networked match takes
            // the server's instead.
            bool Instagib = false;
            std::int32_t Port = 0;
            std::optional<std::string> PlayerName{};
            std::uint8_t SaveSlot = 0;
            bool NewGame = false;
            std::optional<std::string> DemoPath{};
        };

        LaunchPlan() = default;
        explicit LaunchPlan(const Init& init);

        LaunchPlan(const LaunchPlan&) = default;
        LaunchPlan& operator=(const LaunchPlan&) = default;
        LaunchPlan(LaunchPlan&& other);
        LaunchPlan& operator=(LaunchPlan&& other);

        [[nodiscard]] LaunchKind Kind() const noexcept;
        [[nodiscard]] const std::shared_ptr<LobbyContext>& Lobby() const noexcept;
        [[nodiscard]] MphRead::Hunter Hunter() const noexcept;
        [[nodiscard]] const std::optional<std::string>& RoomKey() const noexcept;
        [[nodiscard]] MphRead::GameMode Mode() const noexcept;
        [[nodiscard]] std::int32_t Bots() const noexcept;
        [[nodiscard]] std::int32_t BotLevel() const noexcept;
        [[nodiscard]] bool Instagib() const noexcept;
        [[nodiscard]] std::int32_t Port() const noexcept;
        [[nodiscard]] const std::optional<std::string>& PlayerName() const noexcept;
        [[nodiscard]] std::uint8_t SaveSlot() const noexcept;
        [[nodiscard]] bool NewGame() const noexcept;
        [[nodiscard]] const std::optional<std::string>& DemoPath() const noexcept;

    private:
        LaunchKind _kind = LaunchKind::None;
        std::shared_ptr<LobbyContext> _lobby{};
        MphRead::Hunter _hunter = static_cast<MphRead::Hunter>(0);
        std::optional<std::string> _roomKey{};
        MphRead::GameMode _mode = static_cast<MphRead::GameMode>(0);
        std::int32_t _bots = 0;
        std::int32_t _botLevel = 0;
        bool _instagib = false;
        std::int32_t _port = 0;
        std::optional<std::string> _playerName{};
        std::uint8_t _saveSlot = 0;
        bool _newGame = false;
        std::optional<std::string> _demoPath{};
    };
}

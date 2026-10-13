#include "Mods/Network/Gameplay/Player/PlayerEntityNetHud.hpp"

#include "HUD/HudInfo.hpp"
#include "Mods/Network/Gameplay/Player/NetHudHealth.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/EndScreen.hpp"

#include <algorithm>
#include <optional>

#include <cstdint>
#include <string>

namespace MphRead::Entities
{
    bool PlayerEntity::ModHideOpponentHealth()
    {
        const std::optional<Mods::Network::MatchDefinition> match = Mods::Network::NetSession::ActiveMatchDefinition();
        return match.has_value() && match->HideOpponentHealth;
    }

    std::int32_t PlayerEntity::ModOpponentHudHealth(PlayerEntity& opponent)
    {
        return Mods::Network::NetHudHealth::Sample(opponent).Health;
    }

    bool PlayerEntity::ModHudHealthVisible() const
    {
        return Mods::Network::NetHudHealth::Visible(SlotIndex());
    }

    std::int32_t PlayerEntity::ModHudHealth()
    {
        return !Mods::Network::NetSession::Active() || SlotIndex() == Mods::Network::NetSession::LocalSlot()
            ? Health() : ModOpponentHudHealth(*this);
    }

    float PlayerEntity::ModScoreSqueeze() const
    {
        if (!Mods::EndScreen::Available())
        {
            return 0.0F;
        }
        const float panelLeft = 254.0F - EndPanelWidth() * HudAspectFix();
        const float rightmost = (Mods::Network::NetSession::Active() ? _scoreColumn2Net : _scoreColumn2Solo) + 24.0F;
        return std::clamp(rightmost - (panelLeft - 3.0F), 0.0F, 64.0F);
    }

    float PlayerEntity::ModScoreColumn1() const
    {
        return (Mods::Network::NetSession::Active() ? _scoreColumn1Net : _scoreColumn1Solo) - ModScoreSqueeze();
    }

    float PlayerEntity::ModScoreColumn2() const
    {
        return (Mods::Network::NetSession::Active() ? _scoreColumn2Net : _scoreColumn2Solo) - ModScoreSqueeze();
    }

    float PlayerEntity::ModScoreNameColumn() const
    {
        return 60.0F - ModScoreSqueeze() / 2.0F;
    }

    bool PlayerEntity::ModPingColumnDrawn()
    {
        return Mods::Network::NetSession::Active() && !Mods::EndScreen::Available();
    }

    void PlayerEntity::ModDrawPingHeader(float posY)
    {
        if (!ModPingColumnDrawn())
        {
            return;
        }

        (void)DrawText2D(_pingColumnX,
            posY,
            Hud::Align::Center,
            0,
            "ping",
            ColorRgba(0x3FEFU),
            8);
    }

    void PlayerEntity::ModDrawPingRow(
        float posY, ColorRgba, std::int32_t slot)
    {
        if (!ModPingColumnDrawn())
        {
            return;
        }
        if (slot < 0)
        {
            return;
        }
        const std::int32_t ping = Mods::Network::SessionSlots().PingAt(slot);

        std::string text;
        if (ping <= 0)
        {
            text = "--";
        }
        else if (ping > 999)
        {
            text = "999";
        }
        else
        {
            text = std::to_string(ping);
        }

        (void)DrawText2D(_pingColumnX,
            posY,
            Hud::Align::Center,
            0,
            text,
            PingColor(ping),
            8);
    }

    ColorRgba PlayerEntity::PingColor(std::int32_t ping)
    {
        if (ping <= 0)
        {
            return ColorRgba(120, 120, 140, 255);
        }
        if (ping < 80)
        {
            return ColorRgba(110, 231, 135, 255);
        }
        if (ping < 160)
        {
            return ColorRgba(255, 200, 80, 255);
        }
        return ColorRgba(255, 110, 110, 255);
    }
}

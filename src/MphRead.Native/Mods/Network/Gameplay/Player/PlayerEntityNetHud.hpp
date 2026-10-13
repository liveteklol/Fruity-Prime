#pragma once

#include "Formats/Types.hpp"

#include <cstdint>

#define MPHREAD_PLAYER_ENTITY_NET_HUD_MEMBERS \
private: \
    [[nodiscard]] static bool ModHideOpponentHealth(); \
    [[nodiscard]] static std::int32_t ModOpponentHudHealth(PlayerEntity& opponent); \
    [[nodiscard]] bool ModHudHealthVisible() const; \
    [[nodiscard]] std::int32_t ModHudHealth(); \
    static constexpr float _scoreColumn1Net = 145.0F; \
    static constexpr float _scoreColumn2Net = 193.0F; \
    static constexpr float _scoreColumn1Solo = 160.0F; \
    static constexpr float _scoreColumn2Solo = 215.0F; \
    [[nodiscard]] static bool ModPingColumnDrawn(); \
public: \
    [[nodiscard]] float ModScoreSqueeze() const; \
    [[nodiscard]] float ModScoreColumn1() const; \
    [[nodiscard]] float ModScoreColumn2() const; \
    [[nodiscard]] float ModScoreNameColumn() const; \
 \
    void ModDrawPingHeader(float posY); \
    void ModDrawPingRow(float posY, ColorRgba rowColor, std::int32_t slot); \
 \
private: \
    static constexpr float _pingColumnX = 236.0F; \
 \
    [[nodiscard]] static ColorRgba PingColor(std::int32_t ping);

#ifndef MPHREAD_PLAYER_ENTITY_CANONICAL_HEADER
#include "Entities/Players/PlayerEntity.hpp"
#endif

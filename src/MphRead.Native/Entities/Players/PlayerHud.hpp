#pragma once

#include "../../Formats/Types.hpp"
#include "../../HUD/HudInfo.hpp"
#include "../../Mods/Render/PlayerEntityIconBounds.hpp"
#include "../../Mods/Render/PlayerEntityProHud.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MphRead
{
    class ModelInstance;
    class Node;
}

namespace MphRead::Text
{
    class Font;
}

namespace MphRead::Entities
{
    class EntityBase;
}

#define MPHREAD_PLAYER_HUD_MEMBERS                                                            \
public:                                                                                       \
    [[nodiscard]] static bool ModForceScoreboard() noexcept                                   \
    {                                                                                          \
        return _modForceScoreboard;                                                            \
    }                                                                                          \
    static void SetModForceScoreboard(bool value) noexcept                                        \
    {                                                                                          \
        _modForceScoreboard = value;                                                           \
    }                                                                                          \
    void SetUpHud();                                                                           \
    [[nodiscard]] bool HudReady() const noexcept                                               \
    {                                                                                          \
        return _hudReady;                                                                      \
    }                                                                                          \
    void UpdateHud();                                                                          \
    [[nodiscard]] bool ScanVisor() const noexcept                                              \
    {                                                                                          \
        return _scanVisor;                                                                     \
    }                                                                                          \
    [[nodiscard]] std::uint8_t HudDisruptedState() const noexcept                              \
    {                                                                                          \
        return _hudDisruptedState;                                                             \
    }                                                                                          \
    [[nodiscard]] float HudDisruptionFactor() const noexcept                                   \
    {                                                                                          \
        return _hudDisruptionFactor;                                                           \
    }                                                                                          \
    void HudEndDisrupted();                                                                    \
    [[nodiscard]] std::int32_t HudWhiteoutState() const noexcept                               \
    {                                                                                          \
        return _hudWhiteoutState;                                                              \
    }                                                                                          \
    [[nodiscard]] float HudWhiteoutFactor() const noexcept                                     \
    {                                                                                          \
        return _hudWhiteoutFactor;                                                             \
    }                                                                                          \
    void BeginWhiteout();                                                                      \
    inline static std::array<float, 192> HudWhiteoutTable{};                                   \
    void DrawHudObjects();                                                                     \
    void DrawHudModels();                                                                      \
    void ProcessModeHud();                                                                     \
    std::int32_t _nodesHudState = 0;                                                           \
    std::int32_t _nodesProgressAmount = 0;                                                     \
    void QueueHudMessage(float x, float y, float duration, std::uint8_t category,              \
        std::int32_t messageId, bool dialogHide = false);                                      \
    void QueueHudMessage(float x, float y, std::int32_t maxWidth, float duration,              \
        std::uint8_t category, std::int32_t messageId, bool dialogHide = false);               \
    void QueueHudMessage(float x, float y, float duration, std::uint8_t category,              \
        const std::string& text, bool dialogHide = false);                                     \
    void QueueHudMessage(float x, float y, std::int32_t maxWidth, float duration,              \
        std::uint8_t category, const std::string& text, bool dialogHide = false);              \
    void QueueHudMessage(float x, float y, ::MphRead::Hud::Align align,                        \
        std::int32_t maxWidth, float fontSize, ::MphRead::ColorRgba color, float alpha,        \
        float duration, std::uint8_t category, const std::string& text,                        \
        bool dialogHide = false, std::optional<::MphRead::Language> localizedLanguage          \
            = std::nullopt);                                                                   \
    void ProcessHudMessageQueue();                                                             \
private:                                                                                       \
    struct LocatorInfo final                                                                   \
    {                                                                                          \
        ::OpenTK::Mathematics::Vector3 Position{};                                             \
        std::shared_ptr<::MphRead::ModelInstance> Model{};                                     \
        ::MphRead::ColorRgb Color{};                                                           \
        float Alpha = 0.0F;                                                                    \
                                                                                               \
        LocatorInfo(::OpenTK::Mathematics::Vector3 position,                                   \
            std::shared_ptr<::MphRead::ModelInstance> model,                                   \
            ::MphRead::ColorRgb color, float alpha)                                            \
            : Position(position), Model(std::move(model)), Color(color), Alpha(alpha)          \
        {                                                                                      \
        }                                                                                      \
    };                                                                                         \
    class HudMessage final                                                                     \
    {                                                                                          \
    public:                                                                                    \
        ::OpenTK::Mathematics::Vector2 Position{};                                             \
        float FontSize = 0.0F;                                                                 \
        ::MphRead::ColorRgba Color{};                                                          \
        float Lifetime = 0.0F;                                                                 \
        float Alpha = 0.0F;                                                                    \
        std::uint8_t Category = 0;                                                             \
        std::int32_t MaxWidth = 0;                                                             \
        ::MphRead::Hud::Align Align = ::MphRead::Hud::Align::Left;                             \
        std::array<char16_t, 256> Text{};                                                      \
        bool DialogHide = false;                                                               \
        std::optional<::MphRead::Language> SourceLanguage{};                                 \
    };                                                                                         \
    [[nodiscard]] bool ShowScoreboard() const;                                                 \
    void LoadModeRules();                                                                      \
    void InitHudState();                                                                       \
    void UpdateHealthbars();                                                                   \
    void UpdateAmmoBar();                                                                      \
    void UpdateBoostBombs();                                                                   \
    void UpdateWeaponSelect();                                                                 \
    void UpdateDamageIndicators();                                                             \
    void HudOnFiredShot();                                                                     \
    void ResetReticle();                                                                       \
    void UpdateReticle();                                                                      \
    [[nodiscard]] ::OpenTK::Mathematics::Vector3 GetCrosshairColor() const;                    \
    void HudOnMorphStart();                                                                    \
    void HudOnWeaponSwitch(::MphRead::BeamType beam);                                          \
    void HudOnZoom(bool zoom);                                                                 \
    void HudOnDisrupted();                                                                     \
    void UpdateDisruptedState();                                                               \
    void EndWhiteout();                                                                        \
    void UpdateWhiteoutState();                                                                \
    void UpdateWhiteoutTable(float value);                                                     \
    void AddLocatorInfo(::OpenTK::Mathematics::Vector3 position,                               \
        std::shared_ptr<::MphRead::ModelInstance> inst, ::MphRead::ColorRgb color,             \
        float alpha = 1.0F);                                                                   \
    void DrawLocatorIcons();                                                                   \
    void DrawLocatorIcon(::OpenTK::Mathematics::Vector3 position,                              \
        std::shared_ptr<::MphRead::ModelInstance> inst, ::MphRead::ColorRgb color,             \
        float alpha);                                                                          \
    void DrawEscapeTime();                                                                     \
    [[nodiscard]] std::string FormatTime(float seconds) const;                                 \
    void DrawMatchTime();                                                                      \
    [[nodiscard]] float GetScoreboardRowSpace() const;                                         \
    [[nodiscard]] float GetScoreboardHeight() const;                                           \
    [[nodiscard]] static float Lerp(float first, float second, float by) noexcept;              \
    void DrawScoreboard();                                                                     \
    void DrawScoreboardPlayer(float posX, float posY, ::MphRead::ColorRgba color,              \
        const std::shared_ptr<::MphRead::Hud::HudObjectInstance>& hunter, std::int32_t slot);  \
    void DrawHealthbars();                                                                     \
    void DrawAmmoBar();                                                                        \
    [[nodiscard]] float HudAspectFix() const;                                                  \
    void DrawWeaponList();                                                                     \
    void DrawBoostBombs();                                                                     \
    void DrawMeter(float x, float y, std::int32_t baseAmount, std::int32_t curAmount,          \
        std::int32_t palette, const std::shared_ptr<::MphRead::Hud::HudMeter>& meter,          \
        bool drawText, bool drawTanks, float alpha = 1.0F);                                    \
    void ProcessHudSurvival();                                                                 \
    void ProcessHudBounty();                                                                   \
    void ProcessHudCapture();                                                                  \
    void ProcessHudDefender();                                                                 \
    void ProcessHudNodes();                                                                    \
    void ProcessHudPrimeHunter();                                                              \
    void DrawModeHud();                                                                        \
    void DrawHudAdventure();                                                                   \
    [[nodiscard]] std::string FormatModeScore(std::int32_t slot) const;                        \
    void DrawModeScore(std::int32_t messageId, const std::string& text);                       \
    void DrawHudBattle();                                                                      \
    void DrawHudSurvival();                                                                    \
    void DrawOctolithInst(std::int32_t frame);                                                 \
    void DrawHudBounty();                                                                      \
    void DrawHudCapture();                                                                     \
    void DrawHudDefender();                                                                    \
    void DrawHudNodes();                                                                       \
    void DrawNodesBonuses();                                                                   \
    void DrawNodesIcons();                                                                     \
    void DrawHudPrimeHunter();                                                                 \
    void UpdateDoubleDamageSpeed(std::int32_t speed);                                          \
    void ProcessDoubleDamageHud();                                                             \
    void DrawDoubleDamageHud();                                                                \
    void ProcessCloakHud();                                                                    \
    void DrawCloakHud();                                                                       \
    [[nodiscard]] bool DrawTargetHealthbar(::MphRead::Entities::EntityBase* target);                     \
    void UpdateOpponent(std::int32_t slot);                                                    \
    void ProcessOpponent();                                                                    \
    void DrawOpponent();                                                                       \
    void DrawModeRules();                                                                      \
    [[nodiscard]] static std::int32_t GlyphIndex(const ::MphRead::Text::Font& font,                  \
        std::int32_t ch);                                                                      \
    [[nodiscard]] const ::MphRead::Text::Font& SetUpFont(char16_t firstChar, bool set);              \
    void DrawFps();                                                                            \
    [[nodiscard]] ::OpenTK::Mathematics::Vector2 DrawText2D(float x, float y,                 \
        ::MphRead::Hud::Align type, std::int32_t palette, std::span<const char16_t> text,      \
        std::optional<::MphRead::ColorRgba> color = std::nullopt, float alpha = 1.0F,          \
        float fontSpacing = -1.0F, std::int32_t maxLength = -1, float scale = 1.0F);           \
    [[nodiscard]] ::OpenTK::Mathematics::Vector2 DrawText2D(float x, float y,                 \
        ::MphRead::Hud::Align type, std::int32_t palette, const std::string& text,             \
        std::optional<::MphRead::ColorRgba> color = std::nullopt, float alpha = 1.0F,          \
        float fontSpacing = -1.0F, std::int32_t maxLength = -1, float scale = 1.0F);           \
    [[nodiscard]] ::OpenTK::Mathematics::Vector2 DrawText2D(float x, float y,                 \
        ::MphRead::Hud::Align type, std::int32_t palette, std::span<const char16_t> text,      \
        std::int32_t maxLength);                                                               \
    [[nodiscard]] std::int32_t WrapText(const std::string& text, std::int32_t maxWidth,        \
        std::span<char16_t> dest, std::int32_t maxTiles = 0);                                 \
    [[nodiscard]] std::int32_t WrapText(std::span<const char16_t> text,                        \
        std::int32_t maxWidth, std::span<char16_t> dest, std::int32_t maxTiles = 0);           \
    void ClearHudMessage(std::int32_t mask);                                                   \
    [[nodiscard]] bool IsHudMessageQueued(std::int32_t mask) const;                            \
    void DrawQueuedHudMessages();                                                              \
                                                                                               \
    inline static bool _modForceScoreboard = false;                                            \
    std::shared_ptr<::MphRead::Hud::HudObjects> _hudObjects{};                                 \
    std::shared_ptr<::MphRead::Hud::HudObject> _targetCircleObj{};                             \
    std::shared_ptr<::MphRead::Hud::HudObject> _sniperCircleObj{};                             \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _targetCircleInst{};                    \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _cloakInst{};                           \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _doubleDamageInst{};                    \
    std::array<std::shared_ptr<::MphRead::Hud::HudObjectInstance>, 6> _weaponSelectInsts{};    \
    std::array<std::shared_ptr<::MphRead::Hud::HudObjectInstance>, 6> _selectBoxInsts{};       \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _textInst{};                            \
    std::shared_ptr<::MphRead::Hud::HudMeter> _healthbarMainMeter{};                           \
    std::shared_ptr<::MphRead::Hud::HudMeter> _healthbarSubMeter{};                            \
    std::shared_ptr<::MphRead::Hud::HudMeter> _ammoBarMeter{};                                 \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _weaponIconInst{};                      \
    std::array<std::shared_ptr<::MphRead::Hud::HudObjectInstance>, 9> _weaponListIcons{};      \
    std::array<::MphRead::Entities::IconBounds, 9> _weaponListIconBounds{};                    \
    ::MphRead::Hud::ReadOnlyList<std::uint8_t> _weaponListSheetData = std::make_shared<const std::vector<std::uint8_t>>();                         \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _boostInst{};                           \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _bombInst{};                            \
    std::shared_ptr<::MphRead::Hud::HudMeter> _enemyHealthMeter{};                             \
    std::shared_ptr<::MphRead::Hud::HudMeter> _scanProgressMeter{};                            \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _octolithInst{};                        \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _primeHunterInst{};                     \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _nodesInst{};                           \
    std::shared_ptr<::MphRead::Hud::HudMeter> _nodeProgressMeter{};                            \
    std::shared_ptr<::MphRead::ModelInstance> _damageIndicator{};                              \
    std::array<std::uint16_t, 8> _damageIndicatorTimers{};                                     \
    std::array<std::shared_ptr<::MphRead::Node>, 8> _damageIndicatorNodes{};                   \
    std::shared_ptr<::MphRead::ModelInstance> _playerLocator{};                                \
    std::shared_ptr<::MphRead::ModelInstance> _arrowLocator{};                                 \
    std::shared_ptr<::MphRead::ModelInstance> _nodeLocator{};                                  \
    std::shared_ptr<::MphRead::ModelInstance> _octolithLocator{};                              \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _starsInst{};                           \
    std::array<std::shared_ptr<::MphRead::Hud::HudObjectInstance>, 8> _hunterInsts{};          \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _messageBoxInst{};                      \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _messageSpacerInst{};                   \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _dialogButtonInst{};                    \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _dialogArrowInst{};                     \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _dialogCrystalInst{};                   \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _dialogPickupInst{};                    \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _dialogFrameInst{};                     \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _mapTeleporterInst{};                   \
    std::array<std::shared_ptr<::MphRead::Hud::HudObjectInstance>, 8> _mapOctolithInsts{};     \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _mapLostOctolithInst{};                 \
    std::array<std::shared_ptr<::MphRead::Hud::HudObjectInstance>, 8> _mapArtifactDotInsts{};  \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _mapLegendDoorInst{};                   \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _mapLegendOtherInst{};                  \
    std::shared_ptr<::MphRead::Hud::HudObjectInstance> _mapQuitInst{};                         \
    std::shared_ptr<::MphRead::ModelInstance> _navPlayerPosModel{};                            \
    std::shared_ptr<::MphRead::ModelInstance> _navDoorModel{};                                 \
    std::array<std::shared_ptr<::MphRead::ModelInstance>, 8> _navMapModels{};                  \
    std::shared_ptr<::MphRead::ModelInstance> _filterModel{};                                  \
    bool _showScoreboard = false;                                                              \
    std::int32_t _iceLayerBindingId = -1;                                                      \
    std::int32_t _helmetBindingId = -1;                                                        \
    std::int32_t _helmetDropBindingId = -1;                                                    \
    std::int32_t _visorBindingId = -1;                                                         \
    std::int32_t _scanBindingId = -1;                                                          \
    std::int32_t _pauseBindingId = -1;                                                         \
    std::array<std::int32_t, 5> _dialogBindingIds{{-1, -1, -1, -1, -1}};                      \
    ::MphRead::Hud::ReadOnlyList<::MphRead::ColorRgba> _textPaletteData{};                     \
    ::MphRead::Hud::ReadOnlyList<::MphRead::ColorRgba> _dialogPaletteData{};                   \
    bool _hudReady = false;                                                                    \
    std::shared_ptr<::MphRead::Hud::RulesInfo> _rulesInfo{};                                   \
    std::array<std::optional<std::string>, 8> _rulesLines{};                                   \
    std::array<std::pair<std::int32_t, std::int32_t>, 8> _rulesLengths{};                      \
    float _hudShiftX = 0.0F;                                                                   \
    float _hudShiftY = 0.0F;                                                                   \
    float _objShiftX = 0.0F;                                                                   \
    float _objShiftY = 0.0F;                                                                   \
    bool _hudWeaponMenuOpen = false;                                                           \
    bool _scanVisor = false;                                                                   \
    std::int32_t _healthbarPalette = 0;                                                        \
    bool _healthbarChangedColor = false;                                                       \
    float _healthbarYOffset = 0.0F;                                                            \
    std::int32_t _ammoBarPalette = 0;                                                          \
    bool _ammoBarChangedColor = false;                                                         \
    float _boostBombsYOffset = 0.0F;                                                           \
    std::int32_t _hudPreviousWeaponSelection = -1;                                            \
    std::array<bool, 6> _wheelAvailable{};                                                     \
    std::array<::OpenTK::Mathematics::Vector2, 6> _weaponSelectHome{};                         \
    [[nodiscard]] std::int32_t UpdateWeaponDrag();                                             \
    [[nodiscard]] std::int32_t UpdateWeaponArc();                                              \
    void DrawRadar();                                                                          \
    bool _smallReticle = false;                                                                \
    std::uint16_t _smallReticleTimer = 0;                                                      \
    bool _sniperReticle = false;                                                               \
    bool _hudZoom = false;                                                                     \
    std::uint8_t _hudDisruptedState = 0;                                                       \
    float _hudDisruptionFactor = 0.0F;                                                         \
    std::uint16_t _hudDisruptedTimer = 0;                                                      \
    std::int32_t _hudWhiteoutState = -1;                                                       \
    float _hudWhiteoutFactor = 0.0F;                                                           \
    float _whiteoutAmount = 0.0F;                                                              \
    float _whiteoutTime = 0.0F;                                                                \
    std::vector<LocatorInfo> _locatorInfo = []                                                 \
    {                                                                                          \
        std::vector<LocatorInfo> values;                                                       \
        values.reserve(15);                                                                    \
        return values;                                                                         \
    }();                                                                                       \
    inline static constexpr float _scoreStartSpace = 13.0F;                                   \
    inline static constexpr float _scoreTeamHeaderSpace = 4.0F;                               \
    inline static constexpr float _scoreTeamLineSpace = 18.0F;                                \
    inline static constexpr float _scorePlayerSpace = 28.0F;                                  \
    inline static constexpr float _scoreMinPlayerSpace = 19.0F;                               \
    inline static const std::array<::MphRead::ColorRgba, 9> _weaponListColors =               \
    {                                                                                          \
        ::MphRead::ColorRgba(0xF8, 0x28, 0x28, 255),                                          \
        ::MphRead::ColorRgba(0xF8, 0xF8, 0x08, 255),                                          \
        ::MphRead::ColorRgba(0xF8, 0x28, 0x28, 255),                                          \
        ::MphRead::ColorRgba(0x20, 0xC0, 0x20, 255),                                          \
        ::MphRead::ColorRgba(0xD0, 0x18, 0x18, 255),                                          \
        ::MphRead::ColorRgba(0x98, 0x38, 0xC0, 255),                                          \
        ::MphRead::ColorRgba(0xF8, 0xB0, 0x18, 255),                                          \
        ::MphRead::ColorRgba(0x50, 0x98, 0xD0, 255),                                          \
        ::MphRead::ColorRgba(0xD0, 0xD0, 0xD0, 255)                                           \
    };                                                                                         \
    std::int32_t _nodeBonusOpponent = -1;                                                     \
    bool _mainNodeBonus = false;                                                               \
    std::array<std::int32_t, 8> _teamNodeCounts{};                                            \
    bool _hudIsPrimeHunter = false;                                                            \
    float _primeHunterTextTimer = 0.0F;                                                        \
    std::int32_t _doubleDamageSpeed = 0;                                                       \
    float _doubleDamageTextTimer = 0.0F;                                                       \
    float _doubleDamageIconTimer = 0.0F;                                                       \
    bool _hudCloaking = false;                                                                 \
    float _cloakTextTimer = 0.0F;                                                              \
    float _opponentHealthbarTimer = 0.0F;                                                      \
    std::int32_t _opponentIndex = -1;                                                         \
    std::int32_t _prevScrollingChars = 0;                                                     \
    bool _usingKanjiFont = false;                                                              \
    inline static constexpr float NumberMargin = 3.0F;                                        \
    inline static constexpr float NumberY = 3.0F;                                             \
    inline static constexpr float NumberScale = 0.5F;                                         \
    inline static constexpr float UnitScale = 0.34F;                                          \
    float _textSpacingY = 0.0F;                                                                \
    inline static const std::array<std::shared_ptr<HudMessage>, 20> _hudMessageQueue = []      \
    {                                                                                          \
        std::array<std::shared_ptr<HudMessage>, 20> values{};                                  \
        for (auto& value : values)                                                             \
        {                                                                                      \
            value = std::make_shared<HudMessage>();                                            \
        }                                                                                      \
        return values;                                                                         \
    }();                                                                                       \
    MPHREAD_PLAYER_ENTITY_PRO_HUD_MEMBERS

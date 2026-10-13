#include "PlayerEntityEndScreen.hpp"

#include "../Input/GamepadGlyphs.hpp"
#include "../Input/InputSourceTracker.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../HUD/HudInfo.hpp"
#include "../../Scene.hpp"
#include "../HunterSuits.hpp"
#include "../Launcher/Portable/LaunchPlan.hpp"
#include "Mods/Network/Gameplay/Player/PlayerColors.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>

using ::MphRead::NativeRuntime::RequireReference;

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

    [[nodiscard]] std::string HunterUpperName(MphRead::Hunter hunter)
    {
        switch (hunter)
        {
        case MphRead::Hunter::Samus: return "SAMUS";
        case MphRead::Hunter::Kanden: return "KANDEN";
        case MphRead::Hunter::Trace: return "TRACE";
        case MphRead::Hunter::Sylux: return "SYLUX";
        case MphRead::Hunter::Noxus: return "NOXUS";
        case MphRead::Hunter::Spire: return "SPIRE";
        case MphRead::Hunter::Weavel: return "WEAVEL";
        case MphRead::Hunter::Guardian: return "GUARDIAN";
        case MphRead::Hunter::Random: return "RANDOM";
        }
        return std::to_string(static_cast<std::uint8_t>(hunter));
    }

}

namespace MphRead::Entities
{
    const OpenTK::Mathematics::Vector4 PlayerEntity::_endPanel(0.0F, 0.0F, 0.0F, 0.62F);
    const OpenTK::Mathematics::Vector4 PlayerEntity::_endPanelEdge(1.0F, 1.0F, 1.0F, 0.16F);
    const OpenTK::Mathematics::Vector4 PlayerEntity::_endSwatchEdge(1.0F, 1.0F, 1.0F, 0.9F);
    const OpenTK::Mathematics::Vector4 PlayerEntity::_endSwatchWell(0.0F, 0.0F, 0.0F, 0.45F);
    const ColorRgba PlayerEntity::_endInk(235, 238, 245, 255);
    const ColorRgba PlayerEntity::_endDim(165, 174, 190, 255);
    const ColorRgba PlayerEntity::_endArrow(255, 215, 90, 255);
    const OpenTK::Mathematics::Vector4 PlayerEntity::_endSwatchHover(1.0F, 1.0F, 1.0F, 0.28F);

    float PlayerEntity::EndScale() noexcept
    {
#if defined(__ANDROID__)
        return 1.3F;
#else
        return 1.0F;
#endif
    }

    float PlayerEntity::EndPanelWidth() noexcept
    {
        return 74.0F * EndScale();
    }

    float PlayerEntity::EndPanelHeight() noexcept
    {
        return EndStackHeight * EndScale();
    }

    float PlayerEntity::EndRow(float offset) noexcept
    {
        return EndPanelTop + offset * EndScale();
    }

    float PlayerEntity::EndPortrait() noexcept
    {
        return 26.0F * EndScale();
    }

    float PlayerEntity::EndPreview() noexcept
    {
        return 38.0F * EndScale();
    }

    void PlayerEntity::ModDrawEndScreen()
    {
        if (!Mods::EndScreen::Available())
        {
            return;
        }
        // The deck panel is up over this and asks the same two questions with
        // the program's own controls. The scoreboard beside it is untouched.
        if (Mods::EndScreen::PanelUp())
        {
            return;
        }
        const float aspect = HudAspectFix();
        const float scale = EndScale();
        const float right = 254.0F;
        const float left = right - EndPanelWidth() * aspect;
        const float centre = left + EndPanelWidth() / 2.0F * aspect;
        const float bottom = EndPanelTop + EndPanelHeight();
        const float previewTop = EndRow(EndRowPreview);
        const float previewBottom = previewTop + EndPreview();
        const float previewLeft = centre - EndPreview() / 2.0F * aspect;
        const float previewRight = centre + EndPreview() / 2.0F * aspect;
        Scene::PreviewWanted(true);
        Scene::PreviewLeft(previewLeft / 256.0F);
        Scene::PreviewTop(previewTop / 192.0F);
        Scene::PreviewRight(previewRight / 256.0F);
        Scene::PreviewBottom(previewBottom / 192.0F);
        Scene& scene = RequireReference(_scene);
        const bool preview = scene.ModPreviewDrawn();
        if (preview)
        {
            scene.DrawHudFlatBox(left, EndPanelTop, right, previewTop, _endPanel);
            scene.DrawHudFlatBox(left, previewTop, previewLeft, previewBottom, _endPanel);
            scene.DrawHudFlatBox(previewRight, previewTop, right, previewBottom, _endPanel);
            scene.DrawHudFlatBox(left, previewBottom, right, bottom, _endPanel);
            DrawEndFrame(previewLeft, previewTop, previewRight, previewBottom, aspect);
        }
        else
        {
            scene.DrawHudFlatBox(left, EndPanelTop, right, bottom, _endPanel);
        }
        scene.DrawHudFlatBox(left, EndPanelTop, left + 0.6F * aspect, bottom, _endPanelEdge);

        static_cast<void>(DrawText2D(centre, EndRow(EndRowTitle), Hud::Align::Center, 0,
            "CHOOSE HUNTER", _endDim, 1.0F, 8.0F, -1, 0.5F * scale));

        const std::int32_t hunter = std::clamp(
            static_cast<std::int32_t>(Mods::EndScreen::Hunter()), 0,
            Mods::Launcher::Hunters::Playable - 1);
        if (!preview)
        {
            std::shared_ptr<Hud::HudObjectInstance> portrait{};
            if (hunter < static_cast<std::int32_t>(_hunterInsts.size()))
            {
                portrait = _hunterInsts[static_cast<std::size_t>(hunter)];
            }
            if (portrait)
            {
                portrait->Alpha = 1.0F;
                portrait->PositionX = (centre - EndPortrait() / 2.0F * aspect) / 256.0F;
                portrait->PositionY
                    = (previewTop + (EndPreview() - EndPortrait()) / 2.0F) / 192.0F;
                scene.DrawHudObject(portrait, 1, EndPortrait() / 32.0F);
            }
        }

        const float arrowY = previewTop + EndPreview() / 2.0F - 6.0F * scale;
        const Mods::EndScreen::Hit previous = DrawEndArrow(
            left + 2.0F * aspect, arrowY, "<", Mods::EndScreen::HoveredPrev(), aspect);
        const Mods::EndScreen::Hit forward = DrawEndArrow(
            right - (2.0F + EndArrowBox()) * aspect, arrowY, ">",
            Mods::EndScreen::HoveredNext(), aspect);

        static_cast<void>(DrawText2D(centre, EndRow(EndRowName), Hud::Align::Center, 0,
            HunterUpperName(static_cast<MphRead::Hunter>(hunter)), _endInk,
            1.0F, 8.0F, -1, 0.6F * scale));

        const std::int32_t suit = Mods::EndScreen::Suit();
        DrawEndSuits(static_cast<MphRead::Hunter>(hunter), suit, left, EndRow(EndRowSuits), aspect);
        static_cast<void>(DrawText2D(centre, EndRow(EndRowSuitName), Hud::Align::Center, 0,
            std::string("SUIT: ") + Mods::HunterSuits::Name(
                Mods::HunterSuits::Color(static_cast<MphRead::Hunter>(hunter), suit)),
            _endInk, 1.0F, 8.0F, -1, 0.45F * scale));

        const Mods::EndScreen::Hit ready = DrawEndReady(centre, EndRow(EndRowReady), aspect);
        Mods::EndScreen::NoteLayout(previous, forward, _endSuitHits, ready);

        const std::string next = Mods::EndScreen::NextRoomName();
        if (!next.empty())
        {
            static_cast<void>(DrawText2D(centre, EndRow(EndRowNext), Hud::Align::Center, 0,
                std::string("NEXT: ") + ::MphRead::NativeRuntime::ToUpperInvariant(next),
                _endDim, 1.0F, 8.0F, -1, 0.45F * scale));
        }
        // The ballot, under the picker and in the same column, given the
        // panel's floor rather than working it out again.
        ModDrawMapPick(bottom);
    }

    void PlayerEntity::DrawEndFrame(
        float left, float top, float right, float bottom, float aspect)
    {
        constexpr float line = 0.7F;
        Scene& scene = RequireReference(_scene);
        scene.DrawHudFlatBox(
            left - line * aspect, top - line, right + line * aspect, top, _endPanelEdge);
        scene.DrawHudFlatBox(
            left - line * aspect, bottom, right + line * aspect, bottom + line, _endPanelEdge);
        scene.DrawHudFlatBox(
            left - line * aspect, top, left, bottom, _endPanelEdge);
        scene.DrawHudFlatBox(
            right, top, right + line * aspect, bottom, _endPanelEdge);
    }

    float PlayerEntity::EndArrowBox() noexcept
    {
        return 12.0F * EndScale();
    }

    float PlayerEntity::EndReadyWidth() noexcept
    {
        return 68.0F * EndScale();
    }

    float PlayerEntity::EndReadyHeight() noexcept
    {
        return 14.0F * EndScale();
    }

    Mods::EndScreen::Hit PlayerEntity::DrawEndReady(float centre, float top, float aspect)
    {
        const float half = EndReadyWidth() / 2.0F * aspect;
        const float left = centre - half;
        const float right = centre + half;
        const float bottom = top + EndReadyHeight();
        const bool on = Mods::EndScreen::Ready();
        Scene& scene = RequireReference(_scene);
        scene.DrawHudFlatBox(left, top, right, bottom,
            on ? _endReadyOn
               : Mods::EndScreen::HoveredReady() ? _endArrowHover : _endArrowWell);
        static_cast<void>(DrawText2D(centre, top + 3.5F * EndScale(), Hud::Align::Center, 0,
            on ? std::string("WAITING FOR OTHERS")
               : Mods::Input::InputSourceTracker::Current() == Mods::Input::InputSource::Gamepad
               ? ::MphRead::NativeRuntime::ToUpperInvariant(Mods::Input::GamepadGlyphs::Resolve(Mods::Input::GamepadButtons::A)) + " READY"
               : std::string("READY"),
            on ? _endReadyInk : _endArrow, 1.0F, 8.0F, -1, 0.42F * EndScale()));
        return ModHudHit(left, top, right, bottom);
    }

    Mods::EndScreen::Hit PlayerEntity::DrawEndArrow(
        float x, float y, const std::string& glyph, bool hovered, float aspect)
    {
        const float rightEdge = x + EndArrowBox() * aspect;
        const float bottomEdge = y + EndArrowBox();
        Scene& scene = RequireReference(_scene);
        scene.DrawHudFlatBox(
            x, y, rightEdge, bottomEdge, hovered ? _endArrowHover : _endArrowWell);
        static_cast<void>(DrawText2D(
            x + EndArrowBox() / 2.0F * aspect, y + 2.0F * EndScale(), Hud::Align::Center, 0,
            glyph, _endArrow, 1.0F, 8.0F, -1, 0.9F * EndScale()));
        return ModHudHit(x, y, rightEdge, bottomEdge);
    }

    Mods::EndScreen::Hit PlayerEntity::ModHudHit(
        float left, float top, float right, float bottom) noexcept
    {
        return Mods::EndScreen::Hit(
            left / 256.0F, top / 192.0F, right / 256.0F, bottom / 192.0F);
    }

    const OpenTK::Mathematics::Vector4 PlayerEntity::_endReadyOn(0.35F, 0.85F, 0.4F, 0.42F);
    const ColorRgba PlayerEntity::_endReadyInk(217, 255, 222, 255);
    const OpenTK::Mathematics::Vector4 PlayerEntity::_endArrowWell(1.0F, 1.0F, 1.0F, 0.10F);
    const OpenTK::Mathematics::Vector4 PlayerEntity::_endArrowHover(1.0F, 0.84F, 0.35F, 0.32F);

    std::size_t PlayerEntity::EndSuitHitCount() noexcept
    {
        return static_cast<std::size_t>(Mods::Network::PlayerColors::Count);
    }

    void PlayerEntity::DrawEndSuits(
        MphRead::Hunter hunter, std::int32_t chosen, float left, float top, float aspect)
    {
        const float slot = 16.0F * EndScale();
        const float box = 11.0F * EndScale();
        const float height = 8.0F * EndScale();
        const std::int32_t hoveredSuit = Mods::EndScreen::HoveredSuit();
        const float startX = left
            + (EndPanelWidth() - slot * Mods::Network::PlayerColors::Count) / 2.0F * aspect;
        Scene& scene = RequireReference(_scene);
        for (std::int32_t i = 0; i < Mods::Network::PlayerColors::Count; ++i)
        {
            const float x = startX + (i * slot + (slot - box) / 2.0F) * aspect;
            const ColorRgba color = Mods::HunterSuits::Color(hunter, i);
            if (i == chosen)
            {
                scene.DrawHudFlatBox(x - 1.5F * aspect, top - 1.5F,
                    x + (box + 1.5F) * aspect, top + height + 1.5F, _endSwatchEdge);
            }
            else
            {
                scene.DrawHudFlatBox(x - 1.0F * aspect, top - 1.0F,
                    x + (box + 1.0F) * aspect, top + height + 1.0F, _endSwatchWell);
            }
            scene.DrawHudFlatBox(x, top, x + box * aspect, top + height,
                OpenTK::Mathematics::Vector4(
                    static_cast<float>(color.Red) / 255.0F,
                    static_cast<float>(color.Green) / 255.0F,
                    static_cast<float>(color.Blue) / 255.0F,
                    1.0F));
            if (i == hoveredSuit && i != chosen)
            {
                scene.DrawHudFlatBox(
                    x, top, x + box * aspect, top + height, _endSwatchHover);
            }
            ReplaceHit((*_endSuitHits)[static_cast<std::size_t>(i)], ModHudHit(
                startX + i * slot * aspect, top - 1.5F,
                startX + (i + 1) * slot * aspect, top + height + 1.5F));
        }
    }
}

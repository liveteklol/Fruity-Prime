#include "PlayerEntityVoteHud.hpp"

#include "../Input/InputSourceTracker.hpp"
#include "../Input/PadBindings.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../Entities/Players/DynamicLightEntity.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../HUD/HudInfo.hpp"
#include "../../Renderer.hpp"
#include "../../Scene.hpp"
#include "Mods/Network/UI/Voting/MapVote.hpp"
#include "../../NativeRuntime/System/Managed.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::RequireReference;

namespace
{
}

namespace
{
}

namespace MphRead::Entities
{
    const OpenTK::Mathematics::Vector4 PlayerEntity::_votePanel(0.0F, 0.0F, 0.0F, 0.42F);
    const OpenTK::Mathematics::Vector4 PlayerEntity::_voteAccept(0.24F, 0.78F, 0.33F, 0.40F);
    const OpenTK::Mathematics::Vector4 PlayerEntity::_voteAcceptLit(0.30F, 0.92F, 0.40F, 0.60F);
    const OpenTK::Mathematics::Vector4 PlayerEntity::_voteDeny(0.85F, 0.24F, 0.24F, 0.40F);
    const OpenTK::Mathematics::Vector4 PlayerEntity::_voteDenyLit(0.98F, 0.32F, 0.32F, 0.60F);
    const ColorRgba PlayerEntity::_voteInk(235, 238, 245, 255);
    const ColorRgba PlayerEntity::_voteDimInk(170, 178, 190, 255);

    float PlayerEntity::VoteLeft()
    {
        return NativeRuntime::IsAndroid() ? 52.0F : 6.0F;
    }

    float PlayerEntity::VoteTop()
    {
        return NativeRuntime::IsAndroid() ? 42.0F : 8.0F;
    }

    float PlayerEntity::VoteButtonScale()
    {
        return NativeRuntime::IsAndroid() ? 1.35F : 1.0F;
    }

    float PlayerEntity::VoteButtonWidth()
    {
        return 52.0F * VoteButtonScale();
    }

    float PlayerEntity::VoteButtonHeight()
    {
        return 13.0F * VoteButtonScale();
    }

    float PlayerEntity::VoteButtonGap()
    {
        return 4.0F * VoteButtonScale();
    }

    bool PlayerEntity::VoteByTouch()
    {
        return NativeRuntime::IsAndroid();
    }

    void PlayerEntity::ModDrawVote()
    {
        using Mods::Network::MapVote;

        if (!MapVote::Active() || !IsMainPlayer())
        {
            return;
        }
        if (Mods::EndScreen::Available())
        {
            MapVote::NoteLayout({}, {});
            return;
        }

        float aspect = HudAspectFix();
        std::string prompt = MapVote::PromptLine();
        std::string tally = MapVote::TallyLine();
        if (!MapVote::Answered() && Mods::Input::InputSourceTracker::Current() == Mods::Input::InputSource::Gamepad)
        {
            tally = ::MphRead::NativeRuntime::StringReplace(tally, "F1 YES / F2 NO",
                ::MphRead::NativeRuntime::ToUpperInvariant(Mods::Input::PadBindings::Describe(
                    Mods::Input::PadBindings::Get(Mods::Input::PadAction::Menu))) + " MENU TO VOTE");
        }
        bool buttons = VoteByTouch() && !MapVote::Answered();
        float height = VoteLineHeight * 2.0F + 4.0F
            + (buttons ? VoteButtonHeight() + VoteButtonGap() : 0.0F);
        float width = MathMax(
            118.0F, VoteButtonWidth() * 2.0F + VoteButtonGap() + 6.0F);
        float right = VoteLeft() + width * aspect;

        float panelLeft = VoteLeft();
        float panelTop = VoteTop();
        float panelBottomTop = VoteTop();
        RequireReference(_scene).DrawHudFlatBox(
            panelLeft, panelTop, right, panelBottomTop + height, _votePanel);

        float promptXLeft = VoteLeft();
        float promptTop = VoteTop();
        DrawText2D(promptXLeft + 3.0F * aspect, promptTop + 2.0F, Hud::Align::Left, 0,
            prompt, _voteInk, 1.0F, 8.0F, -1, 0.42F);

        float tallyXLeft = VoteLeft();
        float tallyTop = VoteTop();
        DrawText2D(tallyXLeft + 3.0F * aspect, tallyTop + 2.0F + VoteLineHeight,
            Hud::Align::Left, 0, tally, _voteDimInk, 1.0F, 8.0F, -1, 0.42F);

        if (!buttons)
        {
            MapVote::NoteLayout({}, {});
            return;
        }

        float buttonTop = VoteTop() + VoteLineHeight * 2.0F + 4.0F;
        float bottom = buttonTop + VoteButtonHeight();
        float acceptLeft = VoteLeft() + 3.0F * aspect;
        float acceptRight = acceptLeft + VoteButtonWidth() * aspect;
        float denyLeft = acceptRight + VoteButtonGap() * aspect;
        float denyRight = denyLeft + VoteButtonWidth() * aspect;
        Mods::EndScreen::Hit accept = ModVoteHit(acceptLeft, buttonTop, acceptRight, bottom);
        Mods::EndScreen::Hit deny = ModVoteHit(denyLeft, buttonTop, denyRight, bottom);

        float acceptPointerX = Mods::EndScreen::PointerX();
        float acceptPointerY = Mods::EndScreen::PointerY();
        bool overAccept = accept.Contains(acceptPointerX, acceptPointerY);
        float denyPointerX = Mods::EndScreen::PointerX();
        float denyPointerY = Mods::EndScreen::PointerY();
        bool overDeny = deny.Contains(denyPointerX, denyPointerY);

        RequireReference(_scene).DrawHudFlatBox(
            acceptLeft, buttonTop, acceptRight, bottom, overAccept ? _voteAcceptLit : _voteAccept);
        RequireReference(_scene).DrawHudFlatBox(
            denyLeft, buttonTop, denyRight, bottom, overDeny ? _voteDenyLit : _voteDeny);

        float labelY = buttonTop
            + (VoteButtonHeight() - 16.0F * 0.42F * VoteButtonScale()) / 2.0F;
        DrawText2D((acceptLeft + acceptRight) / 2.0F, labelY, Hud::Align::Center, 0,
            "ACCEPT", _voteInk, 1.0F, 8.0F, -1, 0.42F * VoteButtonScale());
        DrawText2D((denyLeft + denyRight) / 2.0F, labelY, Hud::Align::Center, 0,
            "DENY", _voteInk, 1.0F, 8.0F, -1, 0.42F * VoteButtonScale());
        MapVote::NoteLayout(accept, deny);
    }

    Mods::EndScreen::Hit PlayerEntity::ModVoteHit(
        float left, float top, float right, float bottom)
    {
        return Mods::EndScreen::Hit{
            left / 256.0F, top / 192.0F, right / 256.0F, bottom / 192.0F
        };
    }
}

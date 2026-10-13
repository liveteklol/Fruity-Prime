#include "PlayerEntityChatHud.hpp"

#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../Features.hpp"
#include "../../Scene.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "ChatFont.hpp"
#include "../../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::RequireReference;

namespace
{
    [[nodiscard]] std::u16string ToChatChars(std::string_view text)
    {
        std::u16string result;
        result.reserve(text.size());
        std::size_t offset = 0;
        while (offset < text.size())
        {
            const std::uint8_t first = static_cast<std::uint8_t>(text[offset]);
            if (first < 0x80U)
            {
                result.push_back(static_cast<char16_t>(first));
                offset++;
                continue;
            }

            std::size_t count = 0;
            std::uint32_t codePoint = 0;
            std::uint32_t minimum = 0;
            if ((first & 0xE0U) == 0xC0U)
            {
                count = 2;
                codePoint = first & 0x1FU;
                minimum = 0x80U;
            }
            else if ((first & 0xF0U) == 0xE0U)
            {
                count = 3;
                codePoint = first & 0x0FU;
                minimum = 0x800U;
            }
            else if ((first & 0xF8U) == 0xF0U)
            {
                count = 4;
                codePoint = first & 0x07U;
                minimum = 0x10000U;
            }
            if (count == 0 || offset + count > text.size())
            {
                result.push_back(static_cast<char16_t>(0xFFFD));
                offset++;
                continue;
            }

            bool valid = true;
            for (std::size_t i = 1; i < count; i++)
            {
                const std::uint8_t next = static_cast<std::uint8_t>(text[offset + i]);
                if ((next & 0xC0U) != 0x80U)
                {
                    valid = false;
                    break;
                }
                codePoint = (codePoint << 6U) | (next & 0x3FU);
            }
            if (!valid || codePoint < minimum || codePoint > 0x10FFFFU
                || (codePoint >= 0xD800U && codePoint <= 0xDFFFU))
            {
                result.push_back(static_cast<char16_t>(0xFFFD));
                offset += valid ? count : 1;
                continue;
            }

            offset += count;
            if (codePoint <= 0xFFFFU)
            {
                result.push_back(static_cast<char16_t>(codePoint));
            }
            else
            {
                codePoint -= 0x10000U;
                result.push_back(static_cast<char16_t>(0xD800U + (codePoint >> 10U)));
                result.push_back(static_cast<char16_t>(0xDC00U + (codePoint & 0x3FFU)));
            }
        }
        return result;
    }

    template <typename T>
    [[nodiscard]] ::MphRead::Hud::ReadOnlyList<T> ToReadOnlyList(std::span<T> values)
    {
        return std::make_shared<const std::vector<T>>(values.begin(), values.end());
    }

}

namespace MphRead::Entities
{
    const ColorRgba PlayerEntity::ChatName(110, 255, 130, 255);
    const ColorRgba PlayerEntity::ChatInk(170, 255, 175, 255);
    const ColorRgba PlayerEntity::ChatSystemInk(110, 205, 125, 255);
    const ColorRgba PlayerEntity::ChatPromptInk(110, 205, 125, 255);

    const Hud::ReadOnlyList<ColorRgba> PlayerEntity::_chatPalette
        = std::make_shared<const std::vector<ColorRgba>>(
            std::initializer_list<ColorRgba>{ColorRgba(), ColorRgba(255, 255, 255, 255)});

    float PlayerEntity::ChatLeft(float aspect)
    {
        const float margin = ChatMargin * aspect;
        if (!Features::ModernHud())
        {
            return margin;
        }
        const float scale = std::clamp(Features::WeaponListScale(), 0.6F, 2.0F);
        return 2.0F * aspect + 26.0F * scale * aspect + margin;
    }

    void PlayerEntity::ModDrawChat()
    {
        using Mods::Chat::ChatBox;
        using Mods::Chat::ChatFont;
        using Mods::Network::ChatPacket;

        if (!ChatBox::Visible())
        {
            return;
        }
        Scene& scene = RequireReference(_scene);
        if (!_chatInst)
        {
            _chatInst = std::make_shared<Hud::HudObjectInstance>(ChatFont::Cell, ChatFont::Cell);
            _chatInst->SetPaletteData(_chatPalette, scene);
            _chatInst->SetCharacterData(ToReadOnlyList(ChatFont::Pixels()), scene);
            _chatInst->Enabled = true;
        }
        ChatBox::CollectVisible(_chatVisible);
        const float aspect = HudAspectFix();
        const float x = ChatLeft(aspect);
        float y = ChatPromptY - static_cast<float>(_chatVisible.size()) * ChatLineHeight;
        for (std::size_t i = 0; i < _chatVisible.size(); ++i)
        {
            const auto& [line, alpha] = _chatVisible[i];
            const bool system = line.Kind == ChatPacket::KindSystem;
            std::u16string name;
            if (!system)
            {
                const std::string& lineName = RequireReference(line.Name);
                if (!lineName.empty())
                {
                    name = ToChatChars(lineName);
                    name.append(u": ");
                }
            }
            const float at = ChatDraw(x, y, aspect, alpha, name,
                system ? ChatSystemInk : ChatName);
            std::optional<std::u16string> lineText;
            if (line.Text)
            {
                lineText = ToChatChars(*line.Text);
            }
            const std::optional<std::u16string> fitted
                = ChatFit(lineText, aspect, at - x);
            ChatDraw(at, y, aspect, alpha,
                fitted ? std::u16string_view(*fitted) : std::u16string_view{},
                system ? ChatSystemInk : ChatInk);
            y += ChatLineHeight;
        }
        if (ChatBox::Composing())
        {
            y = ChatPromptY;
            const float at = ChatDraw(x, y, aspect, 1.0F, ChatPrompt, ChatPromptInk);
            std::u16string compose = ToChatChars(ChatBox::ComposeText());
            compose.push_back(u'_');
            ChatDraw(at, y, aspect, 1.0F,
                ChatTail(compose, aspect, at - x), ChatInk);
        }
    }

    float PlayerEntity::ChatDraw(float x, float y, float aspect, float alpha,
        std::u16string_view text, ColorRgba color)
    {
        using Mods::Chat::ChatFont;

        Hud::HudObjectInstance& inst = RequireReference(_chatInst);
        Scene& scene = RequireReference(_scene);
        inst.Alpha = alpha;
        for (char16_t ch : text)
        {
            const std::int32_t index = ChatFont::Index(ch);
            if (index < 0)
            {
                continue;
            }
            if (ch != u' ')
            {
                inst.PositionX = x / 256.0F;
                inst.PositionY = y / 192.0F;
                inst.SetData(index, color, scene);
                scene.DrawHudObject(_chatInst, 1, ChatScale);
            }
            x += static_cast<float>(ChatFont::Widths()[static_cast<std::size_t>(index)])
                * ChatScale * aspect;
        }
        return x;
    }

    float PlayerEntity::ChatWidth(std::u16string_view text, float aspect)
    {
        return static_cast<float>(Mods::Chat::ChatFont::Measure(
            std::span<const char16_t>(text.data(), text.size()))) * ChatScale * aspect;
    }

    float PlayerEntity::ChatRoom(float aspect, float used)
    {
        return 256.0F - ChatLeft(aspect) - ChatMargin * aspect - used;
    }

    std::optional<std::u16string> PlayerEntity::ChatFit(
        const std::optional<std::u16string>& text, float aspect, float used)
    {
        const float room = ChatRoom(aspect, used);
        const std::u16string_view span = text
            ? std::u16string_view(*text)
            : std::u16string_view{};
        if (ChatWidth(span, aspect) <= room)
        {
            return text;
        }
        if (!text)
        {
            throw System::NullReferenceException();
        }
        std::size_t count = text->size();
        while (count > 0 && ChatWidth(std::u16string_view(*text).substr(0, count), aspect) > room)
        {
            --count;
        }
        return text->substr(0, count);
    }

    std::u16string PlayerEntity::ChatTail(std::u16string_view text, float aspect, float used)
    {
        const float room = ChatRoom(aspect, used);
        if (ChatWidth(text, aspect) <= room)
        {
            return std::u16string(text);
        }
        std::size_t start = 0;
        while (start < text.size() && ChatWidth(text.substr(start), aspect) > room)
        {
            ++start;
        }
        return std::u16string(text.substr(start));
    }

    void PlayerEntity::ModForgetInputDeltas()
    {
        _input.Suspend();
        _nativeDual = {};
        _aimFrame = {};
        _buttonAimX = _buttonAimY = 0;
    }
}

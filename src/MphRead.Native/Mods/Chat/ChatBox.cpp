#include "ChatBox.hpp"

#include "../../GameState.hpp"
#include "../InputSettings.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "../../NativeRuntime/System/Exceptions.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::StringIsNullOrWhiteSpace;
using ::MphRead::NativeRuntime::UncheckedIncrement;
using ::MphRead::NativeRuntime::UncheckedSubtract;

namespace
{
    using Keys = OpenTK::Windowing::GraphicsLibraryFramework::Keys;

    constexpr Keys KeyUnknown = static_cast<Keys>(-1);
    constexpr Keys KeyV = static_cast<Keys>(86);
    constexpr Keys KeyEscape = static_cast<Keys>(256);
    constexpr Keys KeyEnter = static_cast<Keys>(257);
    constexpr Keys KeyBackspace = static_cast<Keys>(259);
    constexpr Keys KeyPadEnter = static_cast<Keys>(335);

    [[nodiscard]] std::int64_t TickCount64() noexcept
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    [[nodiscard]] std::string TrimCompose(std::string_view text)
    {
        std::size_t first = 0;
        while (first < text.size() && text[first] == ' ')
        {
            first++;
        }
        std::size_t last = text.size();
        while (last > first && text[last - 1] == ' ')
        {
            last--;
        }
        return std::string(text.substr(first, last - first));
    }
}

namespace MphRead::Mods::Chat
{
    struct ChatBox::State
    {
        std::vector<ChatLine> Lines{};
        std::string Compose{};
        std::mutex Lock{};
        bool Composing = false;
        bool SwallowNextChar = false;
        bool JustClosed = false;
        std::int32_t Sent = 0;
        std::int32_t Received = 0;
    };

    ChatLine::ChatLine()
        : Name(), Text(), Kind(0), ArrivedAt(0)
    {
    }

    ChatLine::ChatLine(std::optional<std::string> name, std::optional<std::string> text,
        std::uint8_t kind, std::int64_t arrivedAt)
        : Name(std::move(name)), Text(std::move(text)), Kind(kind), ArrivedAt(arrivedAt)
    {
    }

    ChatLine& ChatLine::operator=(const ChatLine& other)
    {
        if (this != std::addressof(other))
        {
            std::destroy_at(this);
            std::construct_at(this, other);
        }
        return *this;
    }

    ChatBox::State& ChatBox::GetState()
    {
        static State state;
        return state;
    }

    bool ChatBox::Composing() noexcept
    {
        return GetState().Composing;
    }

    bool ChatBox::ConsumeJustClosed() noexcept
    {
        State& state = GetState();
        if (!state.JustClosed)
        {
            return false;
        }
        state.JustClosed = false;
        return true;
    }

    std::int32_t ChatBox::Sent() noexcept
    {
        return GetState().Sent;
    }

    std::int32_t ChatBox::Received() noexcept
    {
        return GetState().Received;
    }

    std::string ChatBox::ComposeText()
    {
        State& state = GetState();
        std::lock_guard<std::mutex> guard(state.Lock);
        return state.Compose;
    }

    void ChatBox::CollectVisible(std::vector<std::pair<ChatLine, float>>& into)
    {
        into.clear();
        const std::int64_t now = TickCount64();
        State& state = GetState();
        {
            std::lock_guard<std::mutex> guard(state.Lock);
            for (std::int32_t i = static_cast<std::int32_t>(state.Lines.size()) - 1;
                i >= 0 && static_cast<std::int32_t>(into.size()) < VisibleLines; i--)
            {
                const ChatLine& line = state.Lines[static_cast<std::size_t>(i)];
                const std::int64_t age = UncheckedSubtract(now, line.ArrivedAt);
                if (age >= HoldMilliseconds)
                {
                    break;
                }
                const float alpha = age > HoldMilliseconds - FadeMilliseconds
                    ? static_cast<float>(UncheckedSubtract(HoldMilliseconds, age))
                        / static_cast<float>(FadeMilliseconds)
                    : 1.0F;
                into.emplace_back(line, alpha);
            }
        }
        std::reverse(into.begin(), into.end());
    }

    bool ChatBox::Available()
    {
        return !MphRead::GameState::SinglePlayer();
    }

    bool ChatBox::Visible()
    {
        if (!Available())
        {
            return false;
        }
        State& state = GetState();
        if (state.Composing)
        {
            return true;
        }
        const std::int64_t now = TickCount64();
        std::lock_guard<std::mutex> guard(state.Lock);
        return !state.Lines.empty()
            && UncheckedSubtract(now, state.Lines.back().ArrivedAt) < HoldMilliseconds;
    }

    void ChatBox::Clear()
    {
        State& state = GetState();
        {
            std::lock_guard<std::mutex> guard(state.Lock);
            state.Lines.clear();
            state.Compose.clear();
        }
        state.Composing = false;
        state.SwallowNextChar = false;
    }

    void ChatBox::Receive(const Network::ChatPacket& packet)
    {
        if (!packet.Name.has_value())
        {
            throw System::NullReferenceException();
        }
        const std::string name = packet.Name->empty()
            ? "Player" + std::to_string(static_cast<std::uint32_t>(packet.Slot))
            : *packet.Name;
        if (packet.Kind == Network::ChatPacket::KindSystem)
        {
            Add(std::string{}, packet.Text, Network::ChatPacket::KindSystem);
        }
        else
        {
            Add(name, packet.Text, packet.Kind);
        }
        State& state = GetState();
        state.Received = UncheckedIncrement(state.Received);
    }

    void ChatBox::System(const std::optional<std::string>& text)
    {
        Add(std::string{}, text, Network::ChatPacket::KindSystem);
    }

    void ChatBox::Add(const std::optional<std::string>& name,
        const std::optional<std::string>& text, std::uint8_t kind)
    {
        if (!text.has_value() || StringIsNullOrWhiteSpace(*text))
        {
            return;
        }
        State& state = GetState();
        std::lock_guard<std::mutex> guard(state.Lock);
        state.Lines.emplace_back(name, text, kind, TickCount64());
        if (state.Lines.size() > 64U)
        {
            state.Lines.erase(state.Lines.begin(),
                state.Lines.begin() + static_cast<std::ptrdiff_t>(state.Lines.size() - 64U));
        }
    }

    void ChatBox::Open(bool swallowOpeningChar)
    {
        State& state = GetState();
        if (state.Composing || !Available())
        {
            return;
        }
        {
            std::lock_guard<std::mutex> guard(state.Lock);
            state.Compose.clear();
        }
        state.Composing = true;
        state.SwallowNextChar = swallowOpeningChar;
    }

    void ChatBox::Cancel()
    {
        State& state = GetState();
        {
            std::lock_guard<std::mutex> guard(state.Lock);
            state.Compose.clear();
        }
        state.Composing = false;
        state.SwallowNextChar = false;
        state.JustClosed = true;
    }

    void ChatBox::Submit()
    {
        State& state = GetState();
        std::string text;
        {
            std::lock_guard<std::mutex> guard(state.Lock);
            text = TrimCompose(state.Compose);
            state.Compose.clear();
        }
        state.Composing = false;
        state.SwallowNextChar = false;
        state.JustClosed = true;
        if (text.empty())
        {
            return;
        }
        if (text.size() > static_cast<std::size_t>(MaxLength))
        {
            text.resize(static_cast<std::size_t>(MaxLength));
        }
        Send(text);
    }

    void ChatBox::Send(const std::optional<std::string>& text)
    {
        if (!text.has_value())
        {
            throw System::NullReferenceException();
        }
        const bool prefix = ::MphRead::NativeRuntime::StringStartsWithOrdinalIgnoreCase(*text, "/team ");
        const std::optional<Network::MatchDefinition> definition = Network::NetSession::ActiveMatchDefinition();
        const bool team = prefix && GameState::IsTeamMode(definition.has_value() ? definition->Mode : GameState::Mode());
        Add(Network::NetSession::Active() ? Network::NetSession::PlayerName() : "You",
            prefix ? ::MphRead::NativeRuntime::StringTrim(text->substr(6)) : *text,
            team ? Network::ChatPacket::KindTeam : Network::ChatPacket::KindSay);
        if (Network::NetSession::Active())
        {
            if (text.has_value())
            {
                Network::NetSession::SendChat(*text);
            }
            State& state = GetState();
            state.Sent = UncheckedIncrement(state.Sent);
        }
    }

    void ChatBox::HandleText(std::int32_t codePoint)
    {
        State& state = GetState();
        if (!state.Composing)
        {
            return;
        }
        if (state.SwallowNextChar)
        {
            state.SwallowNextChar = false;
            return;
        }
        if (codePoint < 32 || codePoint > 126)
        {
            return;
        }
        std::lock_guard<std::mutex> guard(state.Lock);
        if (state.Compose.size() < static_cast<std::size_t>(MaxLength))
        {
            state.Compose.push_back(static_cast<char>(codePoint));
        }
    }

    bool ChatBox::HandleKeyDown(
        const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e, bool canOpen)
    {
        return HandleKeyDown(e.Key, e.Control, e.Alt || e.Command, canOpen);
    }

    bool ChatBox::HandleKeyDown(
        OpenTK::Windowing::GraphicsLibraryFramework::Keys key,
        bool control, bool alt, bool canOpen, bool swallowOpeningChar)
    {
        State& state = GetState();
        if (!state.Composing)
        {
            if (canOpen && Available() && key != KeyUnknown
                && key == MphRead::Mods::InputSettings::ChatKey() && !alt && !control)
            {
                Open(swallowOpeningChar);
                return true;
            }
            return false;
        }

        if (key == KeyEnter || key == KeyPadEnter)
        {
            Submit();
            return true;
        }
        if (key == KeyEscape)
        {
            Cancel();
            return true;
        }
        if (key == KeyBackspace)
        {
            std::lock_guard<std::mutex> guard(state.Lock);
            if (!state.Compose.empty())
            {
                const std::int32_t end = static_cast<std::int32_t>(state.Compose.size());
                const std::int32_t cut = control ? WordStart(state.Compose, end) : end - 1;
                state.Compose.erase(static_cast<std::size_t>(cut),
                    static_cast<std::size_t>(end - cut));
            }
            return true;
        }
        if (key == KeyV && control)
        {
            return true;
        }
        return true;
    }

    std::int32_t ChatBox::WordStart(const std::string& text, std::int32_t from)
    {
        std::int32_t i = from;
        while (i > 0 && text[static_cast<std::size_t>(i - 1)] == ' ')
        {
            i--;
        }
        while (i > 0 && text[static_cast<std::size_t>(i - 1)] != ' ')
        {
            i--;
        }
        return i;
    }
}

#pragma once

#include "Deck.hpp"
#include "Tap.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::Launcher::Gui
{
    class DeckButton;

    // A small upper-case heading over a group of rows.
    class Caption final : public Av::Controls::Control
    {
    public:
        explicit Caption(std::string text);
        void Render(Av::Media::DrawingContext& context) override;

    protected:
        Av::Size MeasureOverride(Av::Size availableSize) override;

    private:
        [[nodiscard]] const Av::Media::FormattedText& Label() const;
        const std::string _text;
        mutable std::optional<Av::Media::FormattedText> _labelLayout{};
    };

    // One setting with a fixed set of answers: a label, the current answer,
    // and an arrow on each side.
    class ChoiceRow final : public Av::Controls::Control
    {
    public:
        ChoiceRow(std::string label, std::vector<std::string> options, std::int32_t index = 0);

        Av::Event<ChoiceRow&> Changed;

        [[nodiscard]] std::int32_t Index() const noexcept { return _index; }
        void Index(std::int32_t value);
        [[nodiscard]] std::string Value() const { return _options.empty() ? std::string() : _options[static_cast<std::size_t>(_index)]; }
        [[nodiscard]] const std::string& Label() const noexcept { return _label; }

        // Replace the options in place, e.g. after a room list changes.
        void SetItems(std::vector<std::string> options, std::int32_t index = 0);

        // Drawn in a square at the right-hand end of the row, when the answer
        // is a thing better shown than named.
        [[nodiscard]] const std::function<void(Av::Media::DrawingContext&, Av::Rect)>& Preview() const noexcept { return _preview; }
        void Preview(std::function<void(Av::Media::DrawingContext&, Av::Rect)> value);

        void Render(Av::Media::DrawingContext& context) override;

    protected:
        void OnPointerMoved(Av::Input::PointerEventArgs& e) override;
        void OnPointerExited(Av::Input::PointerEventArgs& e) override;
        void OnPointerPressed(Av::Input::PointerPressedEventArgs& e) override;
        void OnPointerReleased(Av::Input::PointerReleasedEventArgs& e) override;
        void OnPointerCaptureLost(Av::Input::PointerCaptureLostEventArgs& e) override;
        void OnKeyDown(Av::Input::KeyEventArgs& e) override;

    private:
        static constexpr double ArrowWidth = 28;
        static constexpr double ValueColumn = 180;
        static constexpr double PreviewWidth = 52;

        [[nodiscard]] double PreviewRoom() const { return _preview ? PreviewWidth : 0; }
        [[nodiscard]] Av::Rect LeftArrow() const;
        [[nodiscard]] Av::Rect RightArrow() const;
        [[nodiscard]] const Av::Media::FormattedText& LabelLayout() const;
        [[nodiscard]] const Av::Media::FormattedText& ValueLayout(double room) const;
        void ClearValueLayout() noexcept;
        void Step(std::int32_t direction);
        static void Arrow(Av::Media::DrawingContext& context, Av::Rect area, bool pointsLeft, bool hot);

        const std::string _label;
        std::vector<std::string> _options;
        std::int32_t _index = 0;
        mutable std::optional<Av::Media::FormattedText> _labelLayout{};
        mutable std::optional<Av::Media::FormattedText> _valueLayout{};
        mutable std::optional<double> _valueLayoutRoom{};
        bool _leftHot = false;
        bool _rightHot = false;
        Tap _tap;
        std::function<void(Av::Media::DrawingContext&, Av::Rect)> _preview;
    };

    // One setting that is on or off.
    class ToggleRow final : public Av::Controls::Control
    {
    public:
        ToggleRow(std::string label, bool on);

        Av::Event<ToggleRow&> Changed;

        [[nodiscard]] bool On() const noexcept { return _on; }
        void On(bool value);

        void Render(Av::Media::DrawingContext& context) override;

    protected:
        void OnPointerPressed(Av::Input::PointerPressedEventArgs& e) override;
        void OnPointerMoved(Av::Input::PointerEventArgs& e) override;
        void OnPointerReleased(Av::Input::PointerReleasedEventArgs& e) override;
        void OnPointerExited(Av::Input::PointerEventArgs& e) override;
        void OnPointerCaptureLost(Av::Input::PointerCaptureLostEventArgs& e) override;
        void OnKeyDown(Av::Input::KeyEventArgs& e) override;

    private:
        const std::string _label;
        bool _on = false;
        mutable std::optional<Av::Media::FormattedText> _labelLayout{};
        Tap _tap;
    };

    // A labelled two-button boolean choice: both answers visible at once.
    class ButtonToggleRow final : public Av::Controls::Grid
    {
    public:
        explicit ButtonToggleRow(const std::string& label, bool on = false);

        Av::Event<ButtonToggleRow&> Changed;

        [[nodiscard]] bool On() const noexcept { return _on; }
        void On(bool value);

    private:
        void Mark();

        std::shared_ptr<DeckButton> _off;
        std::shared_ptr<DeckButton> _onButton;
        bool _on = false;
    };

    // A label and something to type in.
    class FieldRow final : public Av::Controls::Panel
    {
    public:
        FieldRow(const std::string& label, const std::string& value, double boxWidth = 150, bool compact = false);

        const std::shared_ptr<Av::Controls::TextBox> Box;

        [[nodiscard]] std::string Label() const { return _caption->Text(); }
        void Label(std::string value) { _caption->Text(std::move(value)); }
        [[nodiscard]] std::string Value() const { return Box->Text(); }
        void Value(std::string value) { Box->Text(std::move(value)); }

    private:
        std::shared_ptr<Av::Controls::TextBlock> _caption;
    };

    // A line of explanation, wrapped, under a group of rows.
    class Note final : public Av::Controls::TextBlock
    {
    public:
        explicit Note(const std::string& text, std::optional<Av::Media::Color> color = std::nullopt, std::int32_t lines = 2);

    protected:
        Av::Size MeasureOverride(Av::Size availableSize) override;

    private:
        // How many lines it is held to, or 0 for as many as it takes.
        const std::int32_t _lines;
    };
}

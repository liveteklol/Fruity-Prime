#include "GamepadMonitorItem.hpp"

#include "../../MphRead.Native/Mods/Input/GamepadManager.hpp"
#include "../../MphRead.Native/Mods/Input/GamepadOptions.hpp"
#include "../../MphRead.Native/Mods/Input/GamepadProbe.hpp"
#include "../../MphRead.Native/Mods/Input/PadBindings.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Number.hpp"

#include <QtGui/QFont>
#include <QtGui/QFontMetricsF>
#include <QtGui/QPainter>

#include <algorithm>
#include <string>

namespace MphRead::Qt
{
    namespace
    {
        namespace PadInput = ::MphRead::Mods::Input;
        namespace Runtime = ::MphRead::NativeRuntime;

        const QColor Text(230, 234, 242);
        const QColor TextDim(138, 147, 166);
        const QColor PanelLight(26, 31, 41);
        const QColor Accent(255, 179, 71);

        [[nodiscard]] QString Q(const std::string& text)
        {
            return QString::fromStdString(text);
        }
    }

    GamepadMonitorItem::GamepadMonitorItem(QQuickItem* parent) : QQuickPaintedItem(parent)
    {
        setImplicitHeight(182);
        setAntialiasing(true);
        _timer.setInterval(100);
        connect(&_timer, &QTimer::timeout, this, [this]() { if (isVisible()) update(); });
        _timer.start();
    }

    void GamepadMonitorItem::paint(QPainter* painter)
    {
        QPainter& p = *painter;
        p.setRenderHint(QPainter::Antialiasing);
        const std::optional<PadInput::GamepadDeviceSnapshot> device = PadInput::GamepadManager::ActiveDevice();
        const PadInput::GamepadSnapshot snapshot = PadInput::GamepadManager::Snapshot();
        const PadInput::GamepadState& state = snapshot.State;
        const PadInput::GamepadState raw = device.has_value() ? device->RawState() : PadInput::GamepadState{};
        const double w = width();

        QFont font(QStringLiteral("Inter"));
        font.setPixelSize(11);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 11 * 0.04);
        p.setFont(font);
        const auto text = [&](const std::string& value, double x, double y, double width, bool dim = false)
        {
            p.setPen(dim ? TextDim : Text);
            const QString elided = QFontMetricsF(font).elidedText(Q(value), ::Qt::ElideRight, std::max(1.0, width));
            p.drawText(QRectF(x, y, std::max(1.0, width), 25), ::Qt::AlignLeft | ::Qt::AlignTop, elided);
        };

        text(device.has_value() ? device->Name() + " | " + device->Mapping()
                                : "No controller detected. Connect it and press a button.",
            4, 3, w - 8);
        const auto stick = [&](const std::string& label, double x, float axisX, float axisY, float rawX, float rawY,
                               float dead)
        {
            const QPointF centre(x + 27, 60);
            p.setPen(QPen(TextDim, 1));
            p.setBrush(PanelLight);
            p.drawEllipse(centre, 23, 23);
            p.setBrush(::Qt::NoBrush);
            p.drawEllipse(centre, 23 * dead, 23 * dead);
            p.setPen(QPen(Accent, 1));
            p.drawEllipse(QPointF(centre.x() + rawX * 19, centre.y() - rawY * 19), 4, 4);
            p.setPen(::Qt::NoPen);
            p.setBrush(Accent);
            p.drawEllipse(QPointF(centre.x() + axisX * 19, centre.y() - axisY * 19), 4, 4);
            p.setBrush(::Qt::NoBrush);
            text(label, x, 88, 75, true);
        };
        stick("Left stick", 10, state.LeftX, state.LeftY, raw.LeftX, raw.LeftY, PadInput::GamepadOptions::LeftInner());
        stick("Right stick", 96, state.RightX, state.RightY, raw.RightX, raw.RightY,
            PadInput::GamepadOptions::RightInner());

        const auto trigger = [&](const std::string& label, double y, float value)
        {
            text(label, 190, y - 1, 24, true);
            const double bar = std::clamp(w - 274, 30.0, 160.0);
            p.fillRect(QRectF(218, y, bar, 12), PanelLight);
            p.fillRect(QRectF(218, y, bar * std::clamp(static_cast<double>(value), 0.0, 1.0), 12), Accent);
            const double threshold = 218 + bar * PadInput::GamepadOptions::TriggerThreshold();
            p.setPen(QPen(Text, 1));
            p.drawLine(QPointF(threshold, y), QPointF(threshold, y + 12));
            text(Runtime::ToString(value, "0.00"), 224 + bar, y - 1, 45, true);
        };
        trigger("LT", 42, state.LeftTrigger);
        trigger("RT", 70, state.RightTrigger);
        const bool idle = state.Buttons == PadInput::GamepadButtons::None;
        text(idle ? "Press a button to test it" : PadInput::PadBindings::Describe(state.Buttons), 4, 112, w - 8);
        text(idle ? "Sticks move/aim; triggers should only fill the LT/RT bars."
                  : "Assigned: " + PadInput::GamepadProbe::Actions(state.Buttons),
            4, 135, w - 8, true);
        const std::string rawText = "Raw LT " + Runtime::ToString(raw.LeftTrigger, "0.00") + " RT "
            + Runtime::ToString(raw.RightTrigger, "0.00") + " | center L "
            + Runtime::ToString(PadInput::GamepadOptions::LeftCalibration().CenterX(), "0.00") + ","
            + Runtime::ToString(PadInput::GamepadOptions::LeftCalibration().CenterY(), "0.00") + " R "
            + Runtime::ToString(PadInput::GamepadOptions::RightCalibration().CenterX(), "0.00") + ","
            + Runtime::ToString(PadInput::GamepadOptions::RightCalibration().CenterY(), "0.00");
        text(rawText, 4, 158, w - 8, true);
    }
}

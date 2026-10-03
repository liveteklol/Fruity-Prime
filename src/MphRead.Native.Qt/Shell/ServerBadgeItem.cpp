#include "ServerBadgeItem.hpp"

#include "../../MphRead.Native/Mods/Launcher/Gui/GeoCountry.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Net.hpp"

#include <QtGui/QFont>
#include <QtGui/QPainter>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace MphRead::Qt
{
    namespace
    {
        namespace Runtime = ::MphRead::NativeRuntime;

        constexpr double W = 20;
        constexpr double H = 14;

        enum class Kind { Lan, Local, Internet, Silent };
        enum class Style { V, V2, H, H2, H3, Cross, Swiss, Disc, Union, Union2, Stars, Maple, Spain, Greece, Czech, Chile };

        struct Flag
        {
            Style Kind;
            std::vector<std::uint32_t> Colours;
        };

        // Flags.Table.
        const std::map<std::string, Flag>& Table()
        {
            static const std::map<std::string, Flag> flags{
                {"AR", {Style::H, {0x6f9ec9U, 0xe2e0d8U, 0x6f9ec9U}}},
                {"AT", {Style::H, {0x9e2b2bU, 0xe2e0d8U, 0x9e2b2bU}}},
                {"AU", {Style::Union2, {}}},
                {"BE", {Style::V, {0x16181dU, 0xc9a227U, 0x9e2b2bU}}},
                {"BR", {Style::Disc, {0x2c7a4eU, 0xc9a227U}}},
                {"CA", {Style::Maple, {}}},
                {"CH", {Style::Swiss, {0x9e2b2bU, 0xe2e0d8U}}},
                {"CL", {Style::Chile, {}}},
                {"CO", {Style::H3, {0xc9a227U, 0x22457aU, 0x9e2b2bU}}},
                {"CZ", {Style::Czech, {}}},
                {"DE", {Style::H, {0x16181dU, 0x9e2b2bU, 0xc9a227U}}},
                {"DK", {Style::Cross, {0x9e2b2bU, 0xe2e0d8U}}},
                {"ES", {Style::Spain, {}}},
                {"FI", {Style::Cross, {0xe2e0d8U, 0x22457aU}}},
                {"FR", {Style::V, {0x22457aU, 0xe2e0d8U, 0x9e2b2bU}}},
                {"GB", {Style::Union, {}}},
                {"GR", {Style::Greece, {}}},
                {"HU", {Style::H, {0x9e2b2bU, 0xe2e0d8U, 0x2c7a4eU}}},
                {"ID", {Style::H2, {0x9e2b2bU, 0xe2e0d8U}}},
                {"IE", {Style::V, {0x2c7a4eU, 0xe2e0d8U, 0xc47a33U}}},
                {"IS", {Style::Cross, {0x22457aU, 0xe2e0d8U}}},
                {"IT", {Style::V, {0x2c7a4eU, 0xe2e0d8U, 0x9e2b2bU}}},
                {"JP", {Style::Disc, {0xe2e0d8U, 0x9e2b2bU}}},
                {"MX", {Style::V, {0x1f6b45U, 0xe2e0d8U, 0x9e2b2bU}}},
                {"NL", {Style::H, {0x9e2b2bU, 0xe2e0d8U, 0x22457aU}}},
                {"NO", {Style::Cross, {0x9e2b2bU, 0x22457aU}}},
                {"NZ", {Style::Union2, {}}},
                {"PL", {Style::H2, {0xe2e0d8U, 0x9e2b2bU}}},
                {"PT", {Style::V2, {0x2c7a4eU, 0x9e2b2bU}}},
                {"RO", {Style::V, {0x22457aU, 0xc9a227U, 0x9e2b2bU}}},
                {"RU", {Style::H, {0xe2e0d8U, 0x22457aU, 0x9e2b2bU}}},
                {"SE", {Style::Cross, {0x22457aU, 0xc9a227U}}},
                {"TR", {Style::Disc, {0x9e2b2bU, 0xe2e0d8U}}},
                {"UA", {Style::H2, {0x2b6fa8U, 0xc9a227U}}},
                {"US", {Style::Stars, {}}}};
            return flags;
        }

        [[nodiscard]] QColor C(std::uint32_t hex)
        {
            return QColor((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff);
        }

        [[nodiscard]] double R(double v)
        {
            return std::nearbyint(v);
        }

        // Whole pixels: a flag on a half pixel is a smudge.
        void Fill(QPainter& p, std::uint32_t hex, double x, double y, double w, double h)
        {
            p.fillRect(QRectF(R(x), R(y), R(w), R(h)), C(hex));
        }

        void Outline(QPainter& p, double x, double y)
        {
            p.setPen(QPen(QColor(0, 0, 0, 190), 1));
            p.setBrush(::Qt::NoBrush);
            p.drawRect(QRectF(x + 0.5, y + 0.5, W - 1, H - 1));
        }

        void Union(QPainter& p, double x, double y, double w, double h)
        {
            Fill(p, 0x22457aU, x, y, w, h);
            Fill(p, 0xe2e0d8U, x + w / 2 - 2, y, 4, h);
            Fill(p, 0xe2e0d8U, x, y + h / 2 - 2, w, 4);
            Fill(p, 0x9e2b2bU, x + w / 2 - 1, y, 2, h);
            Fill(p, 0x9e2b2bU, x, y + h / 2 - 1, w, 2);
        }

        void DrawFlag(QPainter& p, const std::string& code, double x, double y)
        {
            const double w = W;
            const double h = H;
            const auto found = Table().find(code);
            if (found == Table().end())
            {
                Fill(p, 0x1b2736U, x, y, W, H);
                QFont font(QStringLiteral("Inter"));
                font.setWeight(QFont::DemiBold);
                font.setPixelSize(12);
                p.setFont(font);
                p.setPen(QColor(138, 147, 166));
                p.drawText(QRectF(x, y, W, H), ::Qt::AlignCenter, QString::fromStdString(code).toUpper());
                Outline(p, x, y);
                return;
            }
            const std::vector<std::uint32_t>& c = found->second.Colours;
            switch (found->second.Kind)
            {
            case Style::V:
                Fill(p, c[0], x, y, w / 3, h);
                Fill(p, c[1], x + w / 3, y, w / 3, h);
                Fill(p, c[2], x + 2 * w / 3, y, w / 3, h);
                break;
            case Style::V2:
                Fill(p, c[0], x, y, w * 0.4, h);
                Fill(p, c[1], x + w * 0.4, y, w * 0.6, h);
                break;
            case Style::H:
            case Style::H3:
                Fill(p, c[0], x, y, w, h / 3);
                Fill(p, c[1], x, y + h / 3, w, h / 3);
                Fill(p, c[2], x, y + 2 * h / 3, w, h / 3);
                break;
            case Style::H2:
                Fill(p, c[0], x, y, w, h / 2);
                Fill(p, c[1], x, y + h / 2, w, h / 2);
                break;
            case Style::Cross:
                Fill(p, c[0], x, y, w, h);
                Fill(p, c[1], x + 6, y, 4, h);
                Fill(p, c[1], x, y + 6, w, 4);
                break;
            case Style::Swiss:
                Fill(p, c[0], x, y, w, h);
                Fill(p, c[1], x + 9, y + 3, 4, 9);
                Fill(p, c[1], x + 6, y + 6, 10, 3);
                break;
            case Style::Disc:
                Fill(p, c[0], x, y, w, h);
                p.setPen(::Qt::NoPen);
                p.setBrush(C(c[1]));
                p.drawEllipse(QPointF(x + w / 2, y + h / 2), 4, 4);
                break;
            case Style::Union:
                Union(p, x, y, w, h);
                break;
            case Style::Union2:
                Fill(p, 0x22457aU, x, y, w, h);
                Union(p, x, y, w / 2, h / 2);
                Fill(p, 0xe2e0d8U, x + 15, y + 4, 2, 2);
                Fill(p, 0xe2e0d8U, x + 17, y + 9, 2, 2);
                Fill(p, 0xe2e0d8U, x + 13, y + 11, 2, 2);
                break;
            case Style::Stars:
                Fill(p, 0xe2e0d8U, x, y, w, h);
                for (int i = 0; i < 4; i++)
                {
                    Fill(p, 0x9e2b2bU, x, y + i * 4, w, 2);
                }
                Fill(p, 0x22457aU, x, y, 10, 8);
                break;
            case Style::Maple:
                Fill(p, 0xe2e0d8U, x, y, w, h);
                Fill(p, 0x9e2b2bU, x, y, 5, h);
                Fill(p, 0x9e2b2bU, x + 17, y, 5, h);
                Fill(p, 0x9e2b2bU, x + 10, y + 4, 2, 8);
                Fill(p, 0x9e2b2bU, x + 8, y + 6, 6, 3);
                break;
            case Style::Spain:
                Fill(p, 0x9e2b2bU, x, y, w, h);
                Fill(p, 0xc9a227U, x, y + 4, w, 7);
                break;
            case Style::Greece:
                Fill(p, 0xe2e0d8U, x, y, w, h);
                for (int i = 0; i < 3; i++)
                {
                    Fill(p, 0x2b6fa8U, x, y + i * 4, w, 2);
                }
                Fill(p, 0x2b6fa8U, x, y, 9, 8);
                Fill(p, 0xe2e0d8U, x + 3, y, 3, 8);
                Fill(p, 0xe2e0d8U, x, y + 3, 9, 2);
                break;
            case Style::Czech:
                Fill(p, 0xe2e0d8U, x, y, w, h / 2);
                Fill(p, 0x9e2b2bU, x, y + h / 2, w, h / 2);
                Fill(p, 0x22457aU, x, y, 4, h);
                Fill(p, 0x22457aU, x + 4, y + 3, 3, 9);
                break;
            case Style::Chile:
                Fill(p, 0xe2e0d8U, x, y, w, h / 2);
                Fill(p, 0x9e2b2bU, x, y + h / 2, w, h / 2);
                Fill(p, 0x22457aU, x, y, 8, 8);
                break;
            }
            Outline(p, x, y);
        }

        [[nodiscard]] std::string_view HostOf(std::string_view endpoint) noexcept
        {
            const std::size_t colon = endpoint.rfind(':');
            return colon != std::string_view::npos && colon > 0 ? endpoint.substr(0, colon) : endpoint;
        }

        [[nodiscard]] Kind KindOf(const std::string& endpoint, bool answered)
        {
            if (!answered)
            {
                return Kind::Silent;
            }
            const std::optional<Runtime::IPAddressValue> parsed = Runtime::IPAddressTryParse(HostOf(endpoint));
            if (!parsed.has_value())
            {
                return Kind::Internet;
            }
            if (Runtime::IPAddressIsLoopback(*parsed))
            {
                return Kind::Local;
            }
            const std::vector<std::uint8_t> b = Runtime::IPAddressGetAddressBytes(*parsed);
            if (b.size() == 4U && (b[0] == 10 || (b[0] == 192 && b[1] == 168)
                || (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 169 && b[1] == 254)))
            {
                return Kind::Lan;
            }
            return Kind::Internet;
        }

        [[nodiscard]] std::string CountryOf(const std::string& endpoint)
        {
            const std::optional<Runtime::IPAddressValue> parsed = Runtime::IPAddressTryParse(HostOf(endpoint));
            if (!parsed.has_value() || parsed->Family != Runtime::IPAddressFamily::InterNetwork)
            {
                return {};
            }
            Runtime::Address address;
            address.Family = Runtime::AddressFamily::InterNetwork;
            std::copy_n(parsed->Bytes.begin(), address.Bytes.size(), address.Bytes.begin());
            return ::MphRead::Mods::Launcher::Gui::GeoCountry::Of(std::optional<Runtime::Address>(address));
        }

        void DrawKind(QPainter& p, Kind kind, double x, double y)
        {
            const QRectF frame(R(x), R(y), W, H);
            const QColor ground = kind == Kind::Lan ? QColor(0x23, 0x2a, 0x36)
                : kind == Kind::Local ? QColor(0x1d, 0x2b, 0x27)
                : kind == Kind::Silent ? QColor(0x1a, 0x1f, 0x29)
                : QColor(0x1b, 0x27, 0x36);
            p.setPen(::Qt::NoPen);
            p.setBrush(ground);
            p.drawRoundedRect(frame, 2, 2);
            const double cx = frame.x();
            const double cy = frame.y();
            const QColor ink = kind == Kind::Lan ? QColor(0x2c, 0x5a, 0x4e)
                : kind == Kind::Local ? QColor(0x5f, 0x9e, 0x72)
                : kind == Kind::Silent ? QColor(0x3a, 0x43, 0x53)
                : QColor(0x3f, 0x7f, 0xa8);
            if (kind == Kind::Silent)
            {
                p.fillRect(QRectF(cx + 4, cy + 7, 14, 2), ink);
                return;
            }
            if (kind == Kind::Lan || kind == Kind::Local)
            {
                const QColor stand(0x8a, 0x93, 0xa6);
                p.fillRect(QRectF(cx + 4, cy + 3, 14, 7), ink);
                p.fillRect(QRectF(cx + 10, cy + 10, 2, 2), stand);
                p.fillRect(QRectF(cx + 7, cy + 12, 8, 2), stand);
                return;
            }
            p.fillRect(QRectF(cx + 4, cy + 3, 14, 9), ink);
            const QColor pale(0x8d, 0xc4, 0xe8);
            p.fillRect(QRectF(cx + 4, cy + 6, 14, 1), pale);
            p.fillRect(QRectF(cx + 10, cy + 3, 2, 9), pale);
            p.fillRect(QRectF(cx + 6, cy + 4, 1, 7), pale);
            p.fillRect(QRectF(cx + 15, cy + 4, 1, 7), pale);
        }
    }

    ServerBadgeItem::ServerBadgeItem(QQuickItem* parent) : QQuickPaintedItem(parent)
    {
        setImplicitWidth(W);
        setImplicitHeight(H);
        setAntialiasing(false);
    }

    void ServerBadgeItem::SetEndpoint(const QString& value)
    {
        if (value != _endpoint)
        {
            _endpoint = value;
            emit changed();
            update();
        }
    }

    void ServerBadgeItem::SetAnswered(bool value)
    {
        if (value != _answered)
        {
            _answered = value;
            emit changed();
            update();
        }
    }

    void ServerBadgeItem::paint(QPainter* painter)
    {
        const std::string endpoint = _endpoint.toStdString();
        const Kind kind = KindOf(endpoint, _answered);
        if (kind == Kind::Internet)
        {
            const std::string code = CountryOf(endpoint);
            if (code.size() == 2U)
            {
                DrawFlag(*painter, code, 0, 0);
                return;
            }
        }
        DrawKind(*painter, kind, 0, 0);
    }
}

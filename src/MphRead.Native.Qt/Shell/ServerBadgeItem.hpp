#pragma once

#include <QtQuick/QQuickPaintedItem>

namespace MphRead::Qt
{
    // ServerBadge and Flags: a twenty by fourteen pixel flag for the
    // country an address is in, or a pictogram for a LAN, this machine, the
    // internet at large, or a server that did not answer.
    class ServerBadgeItem : public QQuickPaintedItem
    {
        Q_OBJECT
        Q_PROPERTY(QString endpoint READ Endpoint WRITE SetEndpoint NOTIFY changed)
        Q_PROPERTY(bool answered READ Answered WRITE SetAnswered NOTIFY changed)

    public:
        explicit ServerBadgeItem(QQuickItem* parent = nullptr);

        [[nodiscard]] QString Endpoint() const { return _endpoint; }
        void SetEndpoint(const QString& value);
        [[nodiscard]] bool Answered() const noexcept { return _answered; }
        void SetAnswered(bool value);

        void paint(QPainter* painter) override;

    signals:
        void changed();

    private:
        QString _endpoint;
        bool _answered = false;
    };
}

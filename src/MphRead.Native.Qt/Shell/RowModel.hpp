#pragma once

#include <QtCore/QAbstractListModel>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>
#include <QtGui/QColor>

#include <functional>
#include <vector>

namespace MphRead::Qt
{
    // One row of a settings page: what the QML row draws, and what changing
    // it does. The row kinds are the launcher's -- caption, note, choice,
    // slider, toggle, field, word, button, key, pad, stand, monitor, tabs.
    struct Row
    {
        QString Type;
        QString Id;
        QString Label;
        QStringList Options;
        int Index = 0;
        int Value = 0;
        int Min = 0;
        int Max = 100;
        int Step = 1;
        double LabelWidth = 120;
        double BoxWidth = 150;
        bool On = false;
        QString Text;
        QColor Colour;
        // For notes: how many lines they are held to, 0 for all of them.
        int Lines = 2;
        // Extra layout: margins above and below, the face of a button.
        double Top = 0;
        double Bottom = 0;
        QString Face;
        // What a choice row shows at its end ("crosshair", "suit").
        QString Preview;
        // The binding a key or pad row stands for.
        int Binding = -1;
        std::function<QString(int)> Format;
        // Text read afresh on every refresh (a key's binding, a pad row's state).
        std::function<QString()> Live;
        // Whatever else one kind of row needs (a pad row's capture state).
        std::function<QVariantMap()> Extra;
        std::function<void(Row&)> Changed;
        std::function<void()> Clicked;
        std::function<bool()> Shown;
        std::function<bool()> Enabled;
        // A word's size in points.
        double TextSize = 15;
    };

    // A page of rows. Every row keeps its delegate while its fields change:
    // only a rebuild resets the model.
    class RowModel final : public QAbstractListModel
    {
        Q_OBJECT
        Q_PROPERTY(int count READ Count NOTIFY countChanged)

    public:
        explicit RowModel(QObject* parent = nullptr);

        [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
        [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
        [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
        [[nodiscard]] int Count() const { return static_cast<int>(_rows.size()); }

        void Reset(std::vector<Row> rows);
        Row& Add(Row row);
        [[nodiscard]] Row* Find(const QString& id);
        [[nodiscard]] std::vector<Row>& Rows() noexcept { return _rows; }
        // Re-read every row (visibility, labels) after something changed.
        Q_INVOKABLE void Refresh();

        Q_INVOKABLE void setIndex(int row, int index);
        Q_INVOKABLE void setValue(int row, int value);
        Q_INVOKABLE void setOn(int row, bool on);
        Q_INVOKABLE void setText(int row, const QString& text);
        Q_INVOKABLE void click(int row);
        Q_INVOKABLE QString format(int row, int value) const;

    signals:
        void countChanged();
        // Something outside this page changed too (a shared row).
        void touched();

    private:
        [[nodiscard]] QVariantMap Map(const Row& row) const;
        std::vector<Row> _rows;
    };
}

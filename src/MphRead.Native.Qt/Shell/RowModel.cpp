#include "RowModel.hpp"

#include <algorithm>
#include <utility>

namespace MphRead::Qt
{
    RowModel::RowModel(QObject* parent) : QAbstractListModel(parent)
    {
    }

    int RowModel::rowCount(const QModelIndex& parent) const
    {
        return parent.isValid() ? 0 : static_cast<int>(_rows.size());
    }

    QHash<int, QByteArray> RowModel::roleNames() const
    {
        return {{::Qt::UserRole, "row"}};
    }

    QVariantMap RowModel::Map(const Row& row) const
    {
        QVariantMap map;
        map.insert(QStringLiteral("type"), row.Type);
        map.insert(QStringLiteral("id"), row.Id);
        map.insert(QStringLiteral("label"), row.Label);
        map.insert(QStringLiteral("options"), row.Options);
        map.insert(QStringLiteral("index"), row.Index);
        map.insert(QStringLiteral("value"), row.Value);
        map.insert(QStringLiteral("min"), row.Min);
        map.insert(QStringLiteral("max"), row.Max);
        map.insert(QStringLiteral("step"), row.Step);
        map.insert(QStringLiteral("labelWidth"), row.LabelWidth);
        map.insert(QStringLiteral("boxWidth"), row.BoxWidth);
        map.insert(QStringLiteral("on"), row.On);
        map.insert(QStringLiteral("text"), row.Text);
        map.insert(QStringLiteral("colour"), row.Colour.isValid() ? QVariant(row.Colour) : QVariant());
        map.insert(QStringLiteral("lines"), row.Lines);
        map.insert(QStringLiteral("top"), row.Top);
        map.insert(QStringLiteral("bottom"), row.Bottom);
        map.insert(QStringLiteral("face"), row.Face);
        map.insert(QStringLiteral("preview"), row.Preview);
        map.insert(QStringLiteral("binding"), row.Binding);
        map.insert(QStringLiteral("shown"), row.Shown ? row.Shown() : true);
        map.insert(QStringLiteral("enabled"), row.Enabled ? row.Enabled() : true);
        map.insert(QStringLiteral("textSize"), row.TextSize);
        map.insert(QStringLiteral("valueText"), row.Format ? row.Format(row.Value) : QString());
        map.insert(QStringLiteral("live"), row.Live ? row.Live() : row.Text);
        map.insert(QStringLiteral("extra"), row.Extra ? row.Extra() : QVariantMap());
        return map;
    }

    QVariant RowModel::data(const QModelIndex& index, int role) const
    {
        if (!index.isValid() || index.row() >= static_cast<int>(_rows.size()) || role != ::Qt::UserRole)
        {
            return {};
        }
        return Map(_rows[static_cast<std::size_t>(index.row())]);
    }

    void RowModel::Reset(std::vector<Row> rows)
    {
        beginResetModel();
        _rows = std::move(rows);
        endResetModel();
        emit countChanged();
    }

    Row& RowModel::Add(Row row)
    {
        const int at = static_cast<int>(_rows.size());
        beginInsertRows(QModelIndex(), at, at);
        _rows.push_back(std::move(row));
        endInsertRows();
        emit countChanged();
        return _rows.back();
    }

    Row* RowModel::Find(const QString& id)
    {
        const auto found = std::find_if(_rows.begin(), _rows.end(), [&](const Row& row) { return row.Id == id; });
        return found == _rows.end() ? nullptr : &*found;
    }

    void RowModel::Refresh()
    {
        if (!_rows.empty())
        {
            emit dataChanged(index(0), index(static_cast<int>(_rows.size()) - 1));
        }
    }

    void RowModel::setIndex(int row, int value)
    {
        if (row < 0 || row >= Count())
        {
            return;
        }
        Row& target = _rows[static_cast<std::size_t>(row)];
        target.Index = value;
        if (target.Changed)
        {
            target.Changed(target);
        }
        Refresh();
        emit touched();
    }

    void RowModel::setValue(int row, int value)
    {
        if (row < 0 || row >= Count())
        {
            return;
        }
        Row& target = _rows[static_cast<std::size_t>(row)];
        target.Value = std::clamp(value, target.Min, target.Max);
        if (target.Changed)
        {
            target.Changed(target);
        }
        Refresh();
        emit touched();
    }

    void RowModel::setOn(int row, bool on)
    {
        if (row < 0 || row >= Count())
        {
            return;
        }
        Row& target = _rows[static_cast<std::size_t>(row)];
        target.On = on;
        if (target.Changed)
        {
            target.Changed(target);
        }
        Refresh();
        emit touched();
    }

    void RowModel::setText(int row, const QString& text)
    {
        if (row < 0 || row >= Count())
        {
            return;
        }
        Row& target = _rows[static_cast<std::size_t>(row)];
        target.Text = text;
        if (target.Changed)
        {
            target.Changed(target);
        }
    }

    void RowModel::click(int row)
    {
        if (row < 0 || row >= Count())
        {
            return;
        }
        // Copied: a click may rebuild the page it came from.
        const std::function<void()> action = _rows[static_cast<std::size_t>(row)].Clicked;
        if (action)
        {
            action();
        }
        Refresh();
        emit touched();
    }

    QString RowModel::format(int row, int value) const
    {
        if (row < 0 || row >= Count())
        {
            return {};
        }
        const Row& target = _rows[static_cast<std::size_t>(row)];
        return target.Format ? target.Format(value) : QString::number(value) + QStringLiteral("%");
    }
}

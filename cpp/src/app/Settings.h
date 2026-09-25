#pragma once

#include <QObject>
#include <QVariantMap>

class QSettings;

namespace fp {

// What the launcher's settings page and cards remember (the C#'s
// settings.json, launcher.txt and controls.txt in one file): the profile,
// display, audio, controls, match rules and the last choices made. QML reads
// `values` (a binding follows every change) and writes through set().
class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
public:
    explicit Settings(QObject* parent = nullptr);
    ~Settings() override;

    QVariantMap values() const { return m_values; }
    Q_INVOKABLE QVariant get(const QString& key) const { return m_values.value(key, defaults().value(key)); }
    Q_INVOKABLE void set(const QString& key, const QVariant& value);
    Q_INVOKABLE void save();
    // Back to what a new install has, for the keys starting with `prefix` ("key." for the bindings).
    Q_INVOKABLE void reset(const QString& prefix);
    Q_INVOKABLE QString filePath() const;

    int integer(const QString& key) const { return get(key).toInt(); }
    double number(const QString& key) const { return get(key).toDouble(); }
    bool flag(const QString& key) const { return get(key).toBool(); }
    QString text(const QString& key) const { return get(key).toString(); }

    static const QVariantMap& defaults();

signals:
    void changed(const QString& key);

private:
    QVariantMap m_values;
    QSettings* m_file = nullptr;
};

} // namespace fp

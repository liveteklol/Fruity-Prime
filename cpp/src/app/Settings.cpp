#include "Settings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace fp {

const QVariantMap& Settings::defaults()
{
    static const QVariantMap values = [] {
        QVariantMap v;
        // Profile (launcher.txt)
        v["player.name"] = QStringLiteral("Player");
        v["player.hunter"] = 0; // 0-6, 7 for random
        v["player.suit"] = 0;   // 0-3
        v["online.server"] = QStringLiteral("net.livetek.fr:27888");
        v["online.directory"] = QStringLiteral("net.livetek.fr:27889");
        // Display
        v["display.fullscreen"] = false;
        v["display.fov"] = 78;       // degrees; the DS's own
        v["display.fpsCap"] = 0;     // 0: the display's rate (VSync)
        v["display.lighting"] = true;
        v["display.fog"] = true;
        v["display.filtering"] = false;
        v["display.fpsCounter"] = false;
        v["display.celShading"] = false;
        v["display.proHud"] = false;
        v["display.crosshair"] = QStringLiteral("Cross"); // Cross, Dot, CrossDot, Circle, Brackets
        v["display.crosshairSize"] = QStringLiteral("Medium");
        v["display.weaponStatic"] = true; // Quake's welded gun, or the DS's drifting one
        v["display.intro"] = true;
        // Audio
        v["audio.sfx"] = 100;
        v["audio.music"] = 80;
        // Controls
        v["mouse.sensitivity"] = 1.0;
        v["mouse.invertY"] = false;
        v["mouse.invertX"] = false;
        v["pad.enabled"] = true;
        v["pad.sensitivity"] = 1.0;
        v["pad.deadzone"] = 0.15;
        v["pad.invertY"] = false;
        v["pad.aimAssist"] = true;
        // Match rules, and the last offline match
        v["match.room"] = QStringLiteral("MP3 PROVING GROUND");
        v["match.mode"] = QStringLiteral("battle");
        v["match.bots"] = 3;
        v["match.botLevel"] = 2;
        v["match.teams"] = 2;
        v["match.friendlyFire"] = false;
        v["match.pointGoal"] = -1;  // -1: the mode's own
        v["match.timeLimit"] = -1;  // minutes, -1: the mode's own, 0: none
        v["match.timeGoal"] = -1;   // seconds
        // Hosting
        v["host.name"] = QString();
        v["host.maps"] = QStringLiteral("MP3 PROVING GROUND");
        v["host.maxPlayers"] = 4;
        v["host.lobby"] = true;
        v["host.dedicated"] = false;
        return v;
    }();
    return values;
}

Settings::Settings(QObject* parent)
    : QObject(parent)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    m_file = new QSettings(QDir(dir).filePath(QStringLiteral("fruityprime.ini")), QSettings::IniFormat, this);
    for (const QString& key : m_file->allKeys()) {
        QVariant value = m_file->value(key);
        const QVariant def = defaults().value(key);
        // QSettings reads every ini value back as text: typed by the default.
        if (def.isValid() && def.typeId() != QMetaType::QString && value.canConvert(def.metaType())) {
            value.convert(def.metaType());
        }
        m_values[key] = value;
    }
    for (auto it = defaults().begin(); it != defaults().end(); ++it) {
        if (!m_values.contains(it.key())) {
            m_values[it.key()] = it.value();
        }
    }
}

Settings::~Settings() { save(); }

void Settings::set(const QString& key, const QVariant& value)
{
    if (m_values.value(key) == value) {
        return;
    }
    m_values[key] = value;
    emit changed(key);
}

void Settings::save()
{
    for (auto it = m_values.begin(); it != m_values.end(); ++it) {
        m_file->setValue(it.key(), it.value());
    }
    m_file->sync();
}

void Settings::reset(const QString& prefix)
{
    for (auto it = defaults().begin(); it != defaults().end(); ++it) {
        if (it.key().startsWith(prefix)) {
            set(it.key(), it.value());
        }
    }
    const QStringList keys = m_values.keys();
    for (const QString& key : keys) {
        if (key.startsWith(prefix) && !defaults().contains(key)) {
            m_values.remove(key);
            m_file->remove(key);
            emit changed(key);
        }
    }
}

QString Settings::filePath() const { return m_file->fileName(); }

} // namespace fp

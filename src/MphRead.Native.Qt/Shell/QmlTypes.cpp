#include "QmlTypes.hpp"

#include "ClassicMenuItem.hpp"
#include "GamepadMonitorItem.hpp"
#include "HunterStandItem.hpp"
#include "PlayModel.hpp"
#include "ServerBadgeItem.hpp"
#include "SettingsModel.hpp"
#include "SetupModel.hpp"
#include "CreateServerModel.hpp"
#include "LobbyModel.hpp"
#include "RowModel.hpp"
#include "ShellBridge.hpp"
#include <QtQml/QQmlEngine>

#include <QtQml/qqml.h>

namespace MphRead::Qt
{
    void RegisterQmlTypes()
    {
        static bool done = false;
        if (done)
        {
            return;
        }
        done = true;
        const char* const uri = "FruityPrime.Launcher";
        qmlRegisterSingletonType<ShellBridge>(uri, 1, 0, "ShellHost",
            [](QQmlEngine*, QJSEngine*) -> QObject*
            {
                auto* bridge = ShellBridge::Current();
                if (bridge == nullptr) qFatal("Qt launcher bridge was not initialized");
                QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
                return bridge;
            });
        qmlRegisterType<PlayModel>(uri, 1, 0, "PlayModel");
        qmlRegisterType<HunterStandItem>(uri, 1, 0, "HunterStand");
        qmlRegisterType<ClassicMenuItem>(uri, 1, 0, "ClassicMenu");
        qmlRegisterType<GamepadMonitorItem>(uri, 1, 0, "GamepadMonitor");
        qmlRegisterType<ServerBadgeItem>(uri, 1, 0, "ServerBadge");
        qmlRegisterType<SettingsModel>(uri, 1, 0, "SettingsModel");
        qmlRegisterType<SetupModel>(uri, 1, 0, "SetupModel");
        qmlRegisterType<CreateServerModel>(uri, 1, 0, "CreateServerModel");
        qmlRegisterType<LobbyModel>(uri, 1, 0, "LobbyModel");
        qmlRegisterUncreatableType<RowModel>(uri, 1, 0, "RowModel", QStringLiteral("owned by a screen model"));
    }
}

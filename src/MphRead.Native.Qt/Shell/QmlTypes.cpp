#include "QmlTypes.hpp"

#include "GamepadMonitorItem.hpp"
#include "HunterStandItem.hpp"
#include "PlayModel.hpp"
#include "ServerBadgeItem.hpp"
#include "SettingsModel.hpp"
#include "SetupModel.hpp"
#include "CreateServerModel.hpp"
#include "LobbyModel.hpp"
#include "RowModel.hpp"

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
        qmlRegisterType<PlayModel>(uri, 1, 0, "PlayModel");
        qmlRegisterType<HunterStandItem>(uri, 1, 0, "HunterStand");
        qmlRegisterType<GamepadMonitorItem>(uri, 1, 0, "GamepadMonitor");
        qmlRegisterType<ServerBadgeItem>(uri, 1, 0, "ServerBadge");
        qmlRegisterType<SettingsModel>(uri, 1, 0, "SettingsModel");
        qmlRegisterType<SetupModel>(uri, 1, 0, "SetupModel");
        qmlRegisterType<CreateServerModel>(uri, 1, 0, "CreateServerModel");
        qmlRegisterType<LobbyModel>(uri, 1, 0, "LobbyModel");
        qmlRegisterUncreatableType<RowModel>(uri, 1, 0, "RowModel", QStringLiteral("owned by a screen model"));
    }
}

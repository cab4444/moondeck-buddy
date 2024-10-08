// system/Qt includes
#include <QApplication>
#include <QDir>
#include <QMainWindow>

// local includes
#include "os/pccontrol.h"
#include "os/sunshineapps.h"
#include "os/systemtray.h"
#include "routing.h"
#include "server/clientids.h"
#include "server/httpserver.h"
#include "server/pairingmanager.h"
#include "shared/appmetadata.h"
#include "shared/loggingcategories.h"
#include "utils/appsettings.h"
#include "utils/logsettings.h"
#include "utils/pairinginput.h"
#include "utils/singleinstanceguard.h"
#include "utils/unixsignalhandler.h"

//---------------------------------------------------------------------------------------------------------------------

// NOLINTNEXTLINE(*-avoid-c-arrays)
int main(int argc, char* argv[])
{
    constexpr int             api_version{4};
    const shared::AppMetadata app_meta{shared::AppMetadata::App::Buddy};

    utils::SingleInstanceGuard guard{app_meta.getAppName()};
    if (!guard.tryToRun())
    {
        return EXIT_SUCCESS;
    }

    QApplication app{argc, argv};
    QCoreApplication::setApplicationName(app_meta.getAppName());

    utils::installSignalHandler();
    utils::LogSettings::getInstance().init(app_meta.getLogPath());
    qCInfo(lc::buddyMain) << "startup. Version:" << EXEC_VERSION;

    const utils::AppSettings app_settings{app_meta.getSettingsPath()};
    utils::LogSettings::getInstance().setLoggingRules(app_settings.getLoggingRules());

    server::ClientIds      client_ids{QDir::cleanPath(app_meta.getSettingsDir() + "/clients.json")};
    server::HttpServer     new_server{api_version, client_ids};
    server::PairingManager pairing_manager{client_ids};

    os::PcControl    pc_control{app_meta, app_settings.getHandledDisplays(), app_settings.getRegistryFileOverride(),
                             app_settings.getSteamBinaryOverride()};
    os::SunshineApps sunshine_apps{app_settings.getSunshineAppsFilepath()};

    const QIcon               icon{":/icons/app.ico"};
    const os::SystemTray      tray{icon, app_meta.getAppName(), pc_control};
    const utils::PairingInput pairing_input;

    // Tray + app
    QObject::connect(&tray, &os::SystemTray::signalQuitApp, &app, &QApplication::quit);

    // Tray + pc control
    QObject::connect(&pc_control, &os::PcControl::signalShowTrayMessage, &tray, &os::SystemTray::slotShowTrayMessage);

    // Pairing manager + pairing input
    QObject::connect(&pairing_manager, &server::PairingManager::signalRequestUserInputForPairing, &pairing_input,
                     &utils::PairingInput::slotRequestUserInputForPairing);
    QObject::connect(&pairing_manager, &server::PairingManager::signalAbortPairing, &pairing_input,
                     &utils::PairingInput::slotAbortPairing);
    QObject::connect(&pairing_input, &utils::PairingInput::signalFinishPairing, &pairing_manager,
                     &server::PairingManager::slotFinishPairing);
    QObject::connect(&pairing_input, &utils::PairingInput::signalPairingRejected, &pairing_manager,
                     &server::PairingManager::slotPairingRejected);

    // HERE WE GO!!! (a.k.a. starting point)
    setupRoutes(new_server, pairing_manager, pc_control, sunshine_apps, app_settings.getPreferHibernation(),
                app_settings.getForceBigPicture(), app_settings.getCloseSteamBeforeSleep(),
                app_settings.getMacAddressOverride());

    client_ids.load();
    if (!new_server.startServer(app_settings.getPort(), ":/ssl/moondeck_cert.pem", ":/ssl/moondeck_key.pem",
                                app_settings.getSslProtocol()))
    {
        qFatal("Failed to start server!");
    }

    QGuiApplication::setQuitOnLastWindowClosed(false);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() { qCInfo(lc::buddyMain) << "shutdown."; });
    qCInfo(lc::buddyMain) << "startup finished.";
    return QCoreApplication::exec();
}

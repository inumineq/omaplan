// Omaplan — plan your tasks with AI: write them in plain text, and see the
// plan as overview, calendar, list or timeline.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include "actions.h"
#include "cli.h"
#include "config.h"
#include "datewords.h"
#include "planmodel.h"
#include "settings.h"
#include "snapshot.h"
#include "store.h"
#include "theme.h"
#include "uilanguage.h"

int main(int argc, char *argv[]) {
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    // Global options, wherever they stand; runCli skips them too.
    QString dbPath, language;
    QStringList command;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--db") && i + 1 < args.size())
            dbPath = args[++i];
        else if (args[i] == QLatin1String("--lang") && i + 1 < args.size())
            language = args[++i];
        else
            command << args[i];
    }

    // Everything except `open` (and no arguments) is a CLI command, which runs
    // without a display.
    // `open --help` prints the help too, and --version the version.
    const bool gui = (command.isEmpty() || command.first() == QLatin1String("open"))
            && !command.contains(QLatin1String("--help")) && !command.contains(QLatin1String("-h"))
            && !command.contains(QLatin1String("--version"));
    if (!gui) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("omaplan");
        app.setApplicationVersion(QStringLiteral(OMAPLAN_VERSION));
        // Text output and error messages in the interface language; --json
        // stays English.
        UiLanguage uiLanguage(&app, language, false);
        // How 12/11 and 12.12 are read: the language's convention and [dates].
        DateConvention::setActive(DateConvention::resolve(AppConfig::load(), uiLanguage.language()));
        Store store(dbPath);
        QTextStream out(stdout);
        QTextStream err(stderr);
        return runCli(args, &store, out, err);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("omaplan");
    app.setApplicationVersion(QStringLiteral(OMAPLAN_VERSION));

    // Matches the window to omaplan.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("omaplan");
    app.setWindowIcon(QIcon::fromTheme("omaplan"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the language changes (the Omarchy language, live).
    UiLanguage uiLanguage(&app, language, true);
    // Right-to-left languages mirror the window.
    const auto applyDirection = [&uiLanguage] {
        QGuiApplication::setLayoutDirection(UiLanguage::formatLocale(uiLanguage.language()).textDirection());
    };
    applyDirection();
    QObject::connect(&uiLanguage, &UiLanguage::changed, &app, applyDirection);

    registerQmlTypes();

    // config.toml, followed live; also sets the date convention.
    Settings settings(&uiLanguage);

    Theme theme;
    // Omarchy's font, live: omarchy-font-set takes effect without a restart.
    theme.followSystemFont();

    Store store(dbPath);
    QString storeError;
    const bool storeOpen = store.open(&storeError);
    if (!storeOpen)
        qWarning() << "omaplan:" << storeError;

    Actions actions(&store);
    // plan.json for the read-only edition: written now, so it exists from the
    // first start, and again after every change made in the window.
    Snapshotter snapshotter(&store, Snapshot::path(store.path()));
    snapshotter.setLanguage(uiLanguage.language());
    QObject::connect(&uiLanguage, &UiLanguage::changed, &snapshotter, [&] {
        snapshotter.setLanguage(uiLanguage.language());
        if (storeOpen)
            snapshotter.write();
    });
    if (storeOpen)
        snapshotter.write();
    PlanModel planModel(&store);
    // The window still opens, and says why the plan is missing.
    if (!storeOpen)
        planModel.setStoreError(QObject::tr("Could not open the plan (%1): %2").arg(store.path(), storeError));
    QObject::connect(&actions, &Actions::planChanged, &planModel, &PlanModel::reload);
    planModel.reload();
    // Live: reload when another process (CLI, agent) commits to the database.
    planModel.startLivePolling(500);

    QQmlApplicationEngine engine;
    QObject::connect(&uiLanguage, &UiLanguage::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("uiLanguage", &uiLanguage);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("settings", &settings);
    engine.rootContext()->setContextProperty("actions", &actions);
    engine.rootContext()->setContextProperty("planModel", &planModel);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}

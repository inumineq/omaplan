// Omaplan — plan your tasks with AI: write them in plain text, and see the
// plan as overview, calendar, list or timeline.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include "actions.h"
#include "cli.h"
#include "planmodel.h"
#include "store.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    QString dbPath;
    for (int i = 0; i < args.size(); ++i)
        if (args[i] == QLatin1String("--db") && i + 1 < args.size())
            dbPath = args[++i];

    // Everything except `open` (and no arguments) is a CLI command, which runs
    // without a display.
    const bool gui = args.isEmpty() || args.first() == QLatin1String("open");
    if (!gui)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("omaplan");
    app.setApplicationVersion("0.1.0");

    // Matches the window to omaplan.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("omaplan");
    app.setWindowIcon(QIcon::fromTheme("omaplan"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    if (!gui) {
        Store store(dbPath);
        QTextStream out(stdout);
        QTextStream err(stderr);
        return runCli(args, &store, out, err);
    }

    registerQmlTypes();

    Theme theme;

    Store store(dbPath);
    QString storeError;
    const bool storeOpen = store.open(&storeError);
    if (!storeOpen)
        qWarning() << "omaplan:" << storeError;

    Actions actions(&store);
    PlanModel planModel(&store);
    // The window still opens, and says why the plan is missing.
    if (!storeOpen)
        planModel.setStoreError(QObject::tr("Could not open the plan (%1): %2").arg(store.path(), storeError));
    QObject::connect(&actions, &Actions::planChanged, &planModel, &PlanModel::reload);
    planModel.reload();
    // Live: reload when another process (CLI, agent) commits to the database.
    planModel.startLivePolling(500);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("actions", &actions);
    engine.rootContext()->setContextProperty("planModel", &planModel);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}

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

    qmlRegisterType<OverviewModel>("Omaplan", 1, 0, "OverviewModel");
    qmlRegisterType<CalendarModel>("Omaplan", 1, 0, "CalendarModel");
    qmlRegisterType<PlanListModel>("Omaplan", 1, 0, "PlanListModel");
    qmlRegisterType<GanttModel>("Omaplan", 1, 0, "GanttModel");

    Theme theme;

    Store store(dbPath);
    QString storeError;
    if (!store.open(&storeError))
        qWarning() << "omaplan:" << storeError; // the window still opens, empty

    Actions actions(&store);
    PlanModel planModel(&store);
    QObject::connect(&actions, &Actions::planChanged, &planModel, &PlanModel::reload);
    planModel.reload();

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

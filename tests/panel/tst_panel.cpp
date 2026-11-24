// The settings panel in the real window, offscreen. Invented data, a fake
// command engine and a fake vibe on PATH (claude is not there); no test calls
// a real AI. Each test opens the panel from the closed window.
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJSValue>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include "actions.h"
#include "config.h"
#include "planmodel.h"
#include "settings.h"
#include "store.h"
#include "theme.h"
#include "uilanguage.h"

namespace {
// Every QML warning since the last check. Some runtime warnings (binding
// loops, "Unable to assign", TypeErrors) only reach the message handler, so
// it collects any warning that names a QML file besides QQmlEngine::warnings.
QStringList qmlWarnings;
QtMessageHandler previousHandler = nullptr;

void collectQmlWarnings(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    static const QRegularExpression qmlLocation(QStringLiteral("qrc:/\\S+\\.qml"));
    if ((type == QtWarningMsg || type == QtCriticalMsg) && qmlLocation.match(message).hasMatch())
        qmlWarnings << message;
    previousHandler(type, context, message);
}
} // namespace

class PanelTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_cfg;
    std::unique_ptr<UiLanguage> m_language;
    std::unique_ptr<Theme> m_theme;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<Store> m_store;
    std::unique_ptr<Actions> m_actions;
    std::unique_ptr<PlanModel> m_model;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    QObject *m_panel = nullptr;

    static void pump(int ms = 120) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(5);
        }
    }
    void key(Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::keyClick(m_window, k, m); pump(); }
    void type(const QString &text) {
        for (const QChar c : text) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(c));
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(c));
            QCoreApplication::sendEvent(m_window, &press);
            QCoreApplication::sendEvent(m_window, &release);
        }
        pump();
    }
    // The visual tree, not QObject children: Repeater delegates have no QObject parent.
    static QQuickItem *find(QQuickItem *root, const QString &name) {
        for (QQuickItem *child : root->childItems()) {
            if (child->objectName() == name)
                return child;
            if (QQuickItem *found = find(child, name))
                return found;
        }
        return nullptr;
    }
    QQuickItem *item(const QString &name) const { return find(m_window->contentItem(), name); }
    QString focusName() const {
        QQuickItem *f = m_window->activeFocusItem();
        return f ? f->objectName() : QString();
    }
    // Clicks the middle of an item; it is scrolled into view first, and the
    // click must land where it is seen, not in a part a clipping parent cuts off.
    void click(QQuickItem *target) {
        if (target->activeFocusOnTab())
            target->forceActiveFocus();
        QQuickItem *flick = item(QStringLiteral("settingsFlick"));
        if (flick && flick->isAncestorOf(target)) {
            const QRectF r = target->mapRectToItem(flick, target->boundingRect());
            qreal y = flick->property("contentY").toReal();
            if (r.bottom() > flick->height())
                y += r.bottom() - flick->height();
            if (r.top() < 0)
                y += r.top();
            flick->setProperty("contentY", y);
        }
        pump();
        const QPointF centre = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
        for (QQuickItem *a = target->parentItem(); a; a = a->parentItem())
            QVERIFY2(!a->clip() || a->contains(a->mapFromScene(centre)),
                     qPrintable(QStringLiteral("%1 is cut off: its centre (%2, %3) is outside a clipping parent")
                                    .arg(target->objectName()).arg(centre.x()).arg(centre.y())));
        const QPoint p = centre.toPoint();
        QTest::mouseClick(m_window, Qt::LeftButton, {}, p);
        pump();
    }
    void writeConfig(const QByteArray &text) {
        QFile f(m_cfg);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text);
        f.close();
        m_settings->reload();
        pump();
    }
    void openPanel(const char *section = "engine") {
        if (m_panel->property("visible").toBool())
            QMetaObject::invokeMethod(m_panel, "close");
        pump();
        key(Qt::Key_Comma, Qt::ControlModifier);
        m_panel->setProperty("section", section);
        pump();
    }
    QString fileText() const {
        QFile f(m_cfg);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    // Any QML warning fails the test it was reported in.
    static void verifyNoQmlWarnings() {
        pump(50);
        QStringList found = std::exchange(qmlWarnings, {});
        found.removeDuplicates();
        QVERIFY2(found.isEmpty(), qPrintable(QStringLiteral("QML warnings:\n") + found.join(QLatin1Char('\n'))));
    }

private slots:
    void initTestCase() {
        previousHandler = qInstallMessageHandler(collectQmlWarnings);
        QVERIFY(m_dir.isValid());
        qputenv("OMAPLAN_DB", m_dir.filePath("plan.db").toLocal8Bit());
        qputenv("OMAPLAN_SNAPSHOT", m_dir.filePath("plan.json").toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", m_dir.filePath("xdg").toLocal8Bit());
        QDir().mkpath(m_dir.filePath("bin"));
        QFile vibe(m_dir.filePath("bin/vibe"));
        QVERIFY(vibe.open(QIODevice::WriteOnly));
        vibe.write("#!/bin/sh\nexit 1\n");
        vibe.close();
        vibe.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        qputenv("PATH", (m_dir.filePath("bin") + QStringLiteral(":/usr/bin:/bin")).toLocal8Bit());
        // A slow fake engine: Test can be cancelled while it runs.
        QFile slow(m_dir.filePath("slow.sh"));
        QVERIFY(slow.open(QIODevice::WriteOnly));
        slow.write(QStringLiteral("cat >/dev/null; sleep 1.5; touch %1; echo '{\"tasks\":[],\"unresolved\":[]}'\n")
                       .arg(m_dir.filePath("slow-finished")).toUtf8());
        slow.close();
        m_cfg = m_dir.filePath("config.toml");
        qputenv("OMAPLAN_CONFIG", m_cfg.toLocal8Bit());
        QFile cfg(m_cfg);
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write(QStringLiteral("[engine]\ncommand = \"sh %1\"\n").arg(slow.fileName()).toUtf8());
        cfg.close();

        QQuickStyle::setStyle(QStringLiteral("Material"));
        // English from the Omarchy language, not --lang, so `language` in
        // config.toml can switch it live.
        QFile omarchyLanguage(m_dir.filePath("language"));
        QVERIFY(omarchyLanguage.open(QIODevice::WriteOnly));
        omarchyLanguage.write("en\n");
        omarchyLanguage.close();
        m_language = std::make_unique<UiLanguage>(qApp, QString(), false, m_dir.filePath("language"));
        QDir().mkpath(m_dir.filePath("omarchy/theme"));
        m_theme = std::make_unique<Theme>(m_dir.filePath("omarchy"));
        m_settings = std::make_unique<Settings>(m_language.get());
        registerQmlTypes();
        m_store = std::make_unique<Store>();
        QString error;
        QVERIFY2(m_store->open(&error), qPrintable(error));
        m_actions = std::make_unique<Actions>(m_store.get());
        for (const QString &project : {QStringLiteral("Blog"), QStringLiteral("constructor"), QStringLiteral("<b>Bold</b>"),
                                       QStringLiteral("A project with a rather long name that will not fit")}) {
            Task t;
            t.title = QStringLiteral("Task for ") + project;
            t.project = project;
            t.start = QDateTime(QDate::currentDate(), QTime(10, 0));
            t.startHasTime = true;
            m_actions->create(t, QStringLiteral("cli"));
        }
        m_model = std::make_unique<PlanModel>(m_store.get());
        connect(m_actions.get(), &Actions::planChanged, m_model.get(), &PlanModel::reload);
        m_model->reload();

        m_engine = std::make_unique<QQmlApplicationEngine>();
        connect(m_engine.get(), &QQmlEngine::warnings, this, [](const QList<QQmlError> &errors) {
            for (const QQmlError &error : errors)
                qmlWarnings << error.toString();
        });
        QQmlContext *context = m_engine->rootContext();
        context->setContextProperty(QStringLiteral("uiLanguage"), m_language.get());
        context->setContextProperty(QStringLiteral("theme"), m_theme.get());
        context->setContextProperty(QStringLiteral("actions"), m_actions.get());
        context->setContextProperty(QStringLiteral("planModel"), m_model.get());
        context->setContextProperty(QStringLiteral("settings"), m_settings.get());
        m_engine->load(QUrl(QStringLiteral("qrc:/Main.qml")));
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        m_window->resize(1280, 800);
        m_window->show();
        m_window->requestActivate();
        pump(300);
        m_panel = item(QStringLiteral("settingsPanel"));
        QVERIFY(m_panel);
        verifyNoQmlWarnings();
    }
    void cleanup() { verifyNoQmlWarnings(); }

    // M1: saving a field in an engine card keeps the card, and the focus moves on.
    void savingAnEngineFieldKeepsTheFocus() {
        openPanel();
        QPointer<QQuickItem> field = item(QStringLiteral("field:engine.vibe.model"));
        QVERIFY(field);
        field->forceActiveFocus();
        type(QStringLiteral("medium"));
        key(Qt::Key_Tab);
        QCOMPARE(AppConfig::load(m_cfg).vibeModel, QStringLiteral("medium"));
        QVERIFY2(field, "the card was rebuilt");
        QCOMPARE(focusName(), QStringLiteral("test:claude"));
        // Enter saves too, and the field keeps the focus.
        QQuickItem *claude = item(QStringLiteral("field:engine.claude.model"));
        claude->forceActiveFocus();
        type(QStringLiteral("opus"));
        key(Qt::Key_Return);
        QCOMPARE(AppConfig::load(m_cfg).claudeModel, QStringLiteral("opus"));
        QCOMPARE(focusName(), QStringLiteral("field:engine.claude.model"));
    }

    // M1: the first click on Test after typing in a field runs the test.
    void firstClickOnTestRunsIt() {
        openPanel();
        QSignalSpy spy(m_actions.get(), &Actions::engineTested);
        QQuickItem *field = item(QStringLiteral("field:engine.claude.model"));
        field->forceActiveFocus();
        type(QStringLiteral("x"));
        click(item(QStringLiteral("test:claude")));
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 3000); // claude is not on PATH: an error at once
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("claude"));
        QVERIFY(AppConfig::load(m_cfg).claudeModel.endsWith(QLatin1Char('x')));
    }

    // M6: Esc in a field restores the stored value; the next Esc closes. Nothing half-typed is saved.
    void escapeRestoresAFieldBeforeClosing() {
        openPanel();
        const QString before = fileText();
        QQuickItem *field = item(QStringLiteral("field:engine.command"));
        field->forceActiveFocus();
        type(QStringLiteral("oops"));
        key(Qt::Key_Escape);
        QVERIFY(m_panel->property("visible").toBool());
        QCOMPARE(field->property("text").toString(), AppConfig::load(m_cfg).command);
        key(Qt::Key_Escape);
        QVERIFY(!m_panel->property("visible").toBool());
        QCOMPARE(fileText(), before);
        // Closing with a half edit (the gear, a click outside) saves nothing either.
        openPanel();
        item(QStringLiteral("field:engine.command"))->forceActiveFocus();
        type(QStringLiteral("oops"));
        QMetaObject::invokeMethod(m_panel, "close");
        pump();
        QCOMPARE(fileText(), before);
        openPanel();
        QCOMPARE(item(QStringLiteral("field:engine.command"))->property("text").toString(), AppConfig::load(m_cfg).command);
        QMetaObject::invokeMethod(m_panel, "close");
    }

    // L8: the timeout field can be emptied (default again); 090 is written as 90.
    void timeoutFieldEmptiesAndNormalises() {
        openPanel();
        QQuickItem *field = item(QStringLiteral("field:engine.timeout"));
        field->forceActiveFocus();
        type(QStringLiteral("090"));
        key(Qt::Key_Return);
        QVERIFY2(fileText().contains("timeout = 90\n"), qPrintable(fileText()));
        key(Qt::Key_A, Qt::ControlModifier);
        key(Qt::Key_Backspace);
        key(Qt::Key_Return);
        QVERIFY2(!fileText().contains("timeout"), qPrintable(fileText()));
        // A rejected value goes back to the stored one; the error stays.
        type(QStringLiteral("5"));
        key(Qt::Key_Return);
        QCOMPARE(field->property("text").toString(), QString());
        QVERIFY(!m_panel->property("problem").toString().isEmpty());
        QMetaObject::invokeMethod(m_panel, "close");
    }

    // L10: a running Test can be cancelled, and closing the panel cancels it.
    void testCanBeCancelled() {
        openPanel();
        QFile::remove(m_dir.filePath("slow-finished"));
        click(item(QStringLiteral("test:command")));
        QCOMPARE(item(QStringLiteral("test:command"))->property("text").toString(), QStringLiteral("Stop"));
        click(item(QStringLiteral("test:command")));
        QCOMPARE(item(QStringLiteral("test:command"))->property("text").toString(), QStringLiteral("Test"));
        click(item(QStringLiteral("test:command")));
        QMetaObject::invokeMethod(m_panel, "close");
        pump(2500);
        QVERIFY2(!QFile::exists(m_dir.filePath("slow-finished")), "the engine kept running");
    }

    // A narrow window (a tiled one), like a wide fallback font, must not push
    // the panel's content past its right edge, where the Test buttons would be
    // cut off and could not be clicked.
    void panelFitsANarrowWindow() {
        const QSize before = m_window->size();
        m_window->resize(560, 800);
        pump();
        QQuickItem *flick = item(QStringLiteral("settingsFlick"));
        for (const char *section : {"engine", "dates", "projects"}) {
            openPanel(section);
            const qreal right = flick->mapToScene({flick->width(), 0}).x();
            // The first visible item that reaches past the panel's right edge.
            std::function<QQuickItem *(QQuickItem *)> overflowing = [&](QQuickItem *i) -> QQuickItem * {
                if (!i->isVisible() || i->width() <= 0)
                    return nullptr;
                if (i->mapToScene({i->width(), 0}).x() > right + 0.5)
                    return i;
                for (QQuickItem *c : i->childItems())
                    if (QQuickItem *found = overflowing(c))
                        return found;
                return nullptr;
            };
            QQuickItem *wide = overflowing(flick->childItems().first());
            QVERIFY2(!wide, wide ? qPrintable(QStringLiteral("%1: %2 \"%3\" ends at %4, the panel at %5")
                                                  .arg(QString::fromLatin1(section),
                                                       QString::fromLatin1(wide->metaObject()->className()),
                                                       wide->property("text").toString())
                                                  .arg(wide->mapToScene({wide->width(), 0}).x())
                                                  .arg(right))
                                 : "");
        }
        openPanel();
        QSignalSpy spy(m_actions.get(), &Actions::engineTested);
        click(item(QStringLiteral("test:claude")));
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 3000);
        QMetaObject::invokeMethod(m_panel, "close");
        m_window->resize(before);
        pump();
    }

    // L11: the panel opens at the top.
    void opensScrolledToTheTop() {
        openPanel();
        QQuickItem *flick = item(QStringLiteral("settingsFlick"));
        flick->setProperty("contentY", 200);
        QMetaObject::invokeMethod(m_panel, "close");
        openPanel();
        QCOMPARE(flick->property("contentY").toReal(), 0.0);
        QMetaObject::invokeMethod(m_panel, "close");
    }

    // L1, L11, L15: odd project names show as text, get their colour, and long ones fit.
    void projectNamesAreText() {
        writeConfig(QByteArrayLiteral("[projects]\nconstructor = \"green\"\n\"<b>Bold</b>\" = \"blue\"\n"));
        openPanel("projects");
        QQuickItem *odd = item(QStringLiteral("tag:constructor"));
        QVERIFY(odd);
        QCOMPARE(odd->property("tint").value<QColor>(), QColor(m_theme->paletteColor(QStringLiteral("green"))));
        QQuickItem *bold = item(QStringLiteral("tag:<b>Bold</b>"));
        QVERIFY(bold);
        QQuickItem *label = find(bold, QStringLiteral("tagLabel"));
        QCOMPARE(label->property("textFormat").toInt(), 0); // Text.PlainText
        QQuickItem *longTag = item(QStringLiteral("tag:A project with a rather long name that will not fit"));
        QQuickItem *firstSwatch = item(QStringLiteral("swatch:A project with a rather long name that will not fit:"));
        QVERIFY(longTag && firstSwatch);
        QVERIFY(longTag->mapToScene({longTag->width(), 0}).x() <= firstSwatch->mapToScene({0, 0}).x());

        // N5: the tag's right border is drawn: its last column looks like its
        // first, not like the panel behind it.
        pump(100);
        const QImage shot = m_window->grabWindow();
        const qreal dpr = shot.devicePixelRatio();
        for (const QString &name : {QStringLiteral("Blog"), QStringLiteral("constructor")}) {
            QQuickItem *tag = item(QStringLiteral("tag:") + name);
            const QPointF left = tag->mapToScene({0, tag->height() / 2});
            const QPointF right = tag->mapToScene({tag->width(), tag->height() / 2});
            const int y = int(left.y() * dpr);
            const QColor first = shot.pixelColor(int(std::floor(left.x() * dpr)), y);
            const QColor last = shot.pixelColor(int(std::ceil(right.x() * dpr)) - 1, y);
            QVERIFY2(qAbs(first.red() - last.red()) + qAbs(first.green() - last.green()) + qAbs(first.blue() - last.blue()) < 24,
                     qPrintable(QStringLiteral("%1: left %2, right %3, x %4").arg(name, first.name(), last.name()).arg(right.x())));
        }
        QMetaObject::invokeMethod(m_panel, "close");
    }

    // N5: Esc after switching section restores the hidden field and closes in one go.
    void escapeAfterASectionSwitchCloses() {
        openPanel("engine");
        QQuickItem *field = item(QStringLiteral("field:engine.vibe.model"));
        field->forceActiveFocus();
        type(QStringLiteral("half"));
        QTest::keyClick(m_window, Qt::Key_2, Qt::AltModifier);
        pump();
        QCOMPARE(m_panel->property("section").toString(), QStringLiteral("dates"));
        key(Qt::Key_Escape);
        QVERIFY(!m_panel->property("visible").toBool());
        QVERIFY(!fileText().contains("half"));
        QCOMPARE(field->property("text").toString(), QString());
    }

    // L3: the hour axes of the week calendar and the timeline follow the 12-hour clock.
    void hourAxesFollowTheClock() {
        writeConfig(QByteArrayLiteral("[dates]\nclock = \"12h\"\n"));
        QObject *root = m_engine->rootObjects().first();
        root->setProperty("calendarMode", "week");
        root->setProperty("currentView", 1);
        pump(300);
        QVERIFY(hasText(m_window->contentItem(), QStringLiteral("1 PM")));
        QVERIFY(!hasText(m_window->contentItem(), QStringLiteral("13:00")));
        root->setProperty("currentView", 3);
        pump(300);
        key(Qt::Key_Plus); // zoom in to one day: the hour ticks show
        key(Qt::Key_Plus);
        static const QRegularExpression twelve(QStringLiteral("^\\d{1,2} [AP]M$"));
        static const QRegularExpression twentyFour(QStringLiteral("^\\d{2}$"));
        QVERIFY(hasText(m_window->contentItem(), twelve));
        QVERIFY(!hasText(m_window->contentItem(), twentyFour));
        writeConfig(QByteArrayLiteral(""));
        pump(200);
        QVERIFY(hasText(m_window->contentItem(), twentyFour));
        root->setProperty("currentView", 0);
    }

    // Dates and translated words re-render when the language or the first day
    // of the week changes while the window is open.
    void datesFollowLiveChanges() {
        QObject *root = m_engine->rootObjects().first();
        root->setProperty("currentView", 0);
        pump(200);
        const QDate today = QDate::currentDate();
        const QString english = QLocale(QLocale::English).toString(today, QStringLiteral("dddd d MMMM"));
        QString danish = QLocale(QLocale::Danish).toString(today, QStringLiteral("dddd d. MMMM"));
        danish[0] = danish[0].toUpper();
        static const QRegularExpression todayEnglish(QStringLiteral("^Today "));
        static const QRegularExpression todayDanish(QStringLiteral("^I dag "));
        QVERIFY(hasText(m_window->contentItem(), english));
        QVERIFY(hasText(m_window->contentItem(), todayEnglish));

        writeConfig(QByteArrayLiteral("language = \"da\"\n"));
        pump(200);
        QCOMPARE(m_language->language(), QStringLiteral("da"));
        QVERIFY2(hasText(m_window->contentItem(), danish), qPrintable(danish));
        QVERIFY(hasText(m_window->contentItem(), todayDanish));
        QVERIFY(!hasText(m_window->contentItem(), english));

        // The week title starts on the configured day: "Week 48 · 22–28 …" from a Sunday.
        root->setProperty("calendarMode", "week");
        root->setProperty("currentView", 1);
        writeConfig(QByteArrayLiteral("[dates]\nweek_start = \"sunday\"\n"));
        pump(200);
        const QDate sunday = today.addDays(-(today.dayOfWeek() % 7));
        QVERIFY2(hasText(m_window->contentItem(), QRegularExpression(QStringLiteral("^Week \\d+ · %1\\D").arg(sunday.day()))),
                 qPrintable(sunday.toString()));
        writeConfig(QByteArrayLiteral(""));
        pump(200);
        const QDate monday = today.addDays(1 - today.dayOfWeek());
        QVERIFY2(hasText(m_window->contentItem(), QRegularExpression(QStringLiteral("^Week \\d+ · %1\\D").arg(monday.day()))),
                 qPrintable(monday.toString()));
        root->setProperty("currentView", 0);
        pump(200);
        QVERIFY(hasText(m_window->contentItem(), english));
    }

    // A proposal's task cards follow tasks added and removed in place.
    void proposalCardsFollowEdits() {
        QQuickItem *view = findType(m_window->contentItem(), "ProposalView");
        QVERIFY(view);
        const QVariantMap proposal{{QStringLiteral("tasks"), QVariantList{QVariantMap{{QStringLiteral("title"), QStringLiteral("Draft the agenda")}}}},
                                   {QStringLiteral("unresolved"), QVariantList{}}};
        QMetaObject::invokeMethod(view, "openSingle", Q_ARG(QVariant, QStringLiteral("draft the agenda")),
                                  Q_ARG(QVariant, proposal), Q_ARG(QVariant, QString()));
        pump();
        QCOMPARE(countType(view, "ProposalTask"), 1);
        QMetaObject::invokeMethod(view, "addTask", Q_ARG(QVariant, 0));
        pump();
        QCOMPARE(countType(view, "ProposalTask"), 2);
        QMetaObject::invokeMethod(view, "removeTask", Q_ARG(QVariant, 0), Q_ARG(QVariant, 0));
        pump();
        QCOMPARE(countType(view, "ProposalTask"), 1);
        QVERIFY(!hasText(view, QStringLiteral("Draft the agenda")));
        QMetaObject::invokeMethod(view, "close");
        pump();
    }

    void cleanupTestCase() {
        m_engine.reset();
        verifyNoQmlWarnings();
        qInstallMessageHandler(previousHandler);
    }

private:
    // QML types have class names like "ProposalTask_QMLTYPE_12".
    static bool isType(QQuickItem *item, const char *type) {
        return QByteArray(item->metaObject()->className()).startsWith(QByteArray(type) + "_QML");
    }
    static QQuickItem *findType(QQuickItem *root, const char *type) {
        for (QQuickItem *child : root->childItems()) {
            if (isType(child, type))
                return child;
            if (QQuickItem *found = findType(child, type))
                return found;
        }
        return nullptr;
    }
    static int countType(QQuickItem *root, const char *type) {
        int count = 0;
        for (QQuickItem *child : root->childItems())
            count += (isType(child, type) ? 1 : 0) + countType(child, type);
        return count;
    }
    // A visible text item showing exactly this text.
    static bool hasText(QQuickItem *root, const QString &text) {
        return hasText(root, QRegularExpression(QLatin1Char('^') + QRegularExpression::escape(text) + QLatin1Char('$')));
    }
    static bool hasText(QQuickItem *root, const QRegularExpression &text) {
        for (QQuickItem *child : root->childItems()) {
            if (!child->isVisible())
                continue;
            if (text.match(child->property("text").toString()).hasMatch() || hasText(child, text))
                return true;
        }
        return false;
    }
};

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omaplan-panel-tests"));
    PanelTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "tst_panel.moc"

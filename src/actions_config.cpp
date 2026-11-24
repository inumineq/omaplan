// The settings in config.toml as actions: `omaplan config`, `omaplan
// engines`, their MCP tools and the settings panel all come through here.

#include "actions.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <memory>

#include "config.h"
#include "dateannotate.h"
#include "datewords.h"
#include "uilanguage.h"

namespace {

// One setting with its value in the file and the value that applies.
QJsonObject entry(const ConfigKey &key, const ConfigFile &file, const AppConfig &config) {
    bool found = false;
    const QString value = file.value(key.section, key.key, &found);
    QString effective = value;
    QString source = found && !value.isEmpty() ? QStringLiteral("file") : QStringLiteral("default");
    if (key.section == QLatin1String("dates") && source != QLatin1String("file")) {
        // auto: the interface language's convention.
        const DateConvention c = DateConvention::resolve(config, UiLanguage::current());
        effective = key.key == QLatin1String("order")        ? c.orderName()
                    : key.key == QLatin1String("dotted")     ? c.dottedName()
                    : key.key == QLatin1String("week_start") ? c.weekStartName()
                                                             : c.clockName();
        source = QStringLiteral("language");
    } else if (key.name == QLatin1String("language") && source != QLatin1String("file")) {
        effective = UiLanguage::current();
        source = QStringLiteral("system");
    } else if (source != QLatin1String("file")) {
        effective = key.defaultValue;
    }
    QJsonArray choices;
    for (const QString &c : key.choices)
        choices.append(c);
    if (key.section == QLatin1String("projects"))
        for (const QString &slot : ConfigKeys::paletteSlots())
            choices.append(slot);
    return {{QStringLiteral("key"), key.name},
            {QStringLiteral("value"), value},
            {QStringLiteral("effective"), effective},
            {QStringLiteral("source"), source},
            {QStringLiteral("choices"), choices},
            {QStringLiteral("description"), key.description}};
}

QString entryLine(const QJsonObject &e) {
    const QString effective = e.value("effective").toString();
    const QString source = e.value("source").toString();
    QString line = e.value("key").toString() + QStringLiteral(" = ")
                   + (effective.isEmpty() ? QCoreApplication::translate("Actions", "(not set)") : effective);
    if (source == QLatin1String("language"))
        line += QStringLiteral("  ") + QCoreApplication::translate("Actions", "(the language's)");
    else if (source == QLatin1String("default") && !effective.isEmpty())
        line += QStringLiteral("  ") + QCoreApplication::translate("Actions", "(default)");
    return line;
}

} // namespace

ActionResult Actions::configList() {
    const ConfigFile file;
    const AppConfig config = AppConfig::load(file.path());
    QJsonArray settings;
    QStringList lines{tr("Settings in %1").arg(file.path())};
    for (const ConfigKey &key : ConfigKeys::all()) {
        const QJsonObject e = entry(key, file, config);
        settings.append(e);
        lines << entryLine(e);
    }
    QJsonObject projects;
    for (auto it = config.projectColors.cbegin(); it != config.projectColors.cend(); ++it) {
        projects.insert(it.key(), it.value());
        lines << QStringLiteral("projects.%1 = %2").arg(it.key(), it.value());
    }
    QJsonObject payload{{QStringLiteral("path"), file.path()},
                        {QStringLiteral("settings"), settings},
                        {QStringLiteral("projects"), projects}};
    return {true, {}, lines.join(QLatin1Char('\n')), payload};
}

ActionResult Actions::configGet(const QString &name) {
    ConfigKey key;
    if (!ConfigKeys::find(name, &key))
        return {false, tr("Unknown setting: %1 (see omaplan config list)").arg(name), {}, {}};
    const ConfigFile file;
    const QJsonObject e = entry(key, file, AppConfig::load(file.path()));
    return {true, {}, e.value("effective").toString(), e};
}

ActionResult Actions::configSet(const QString &name, const QString &rawValue) {
    ConfigKey key;
    if (!ConfigKeys::find(name, &key))
        return {false, tr("Unknown setting: %1 (see omaplan config list)").arg(name), {}, {}};
    QString value = rawValue.trimmed();
    if (!key.choices.isEmpty() || key.section == QLatin1String("projects") || key.name == QLatin1String("language"))
        value = value.toLower();
    if (key.section == QLatin1String("dates") && value == QLatin1String("auto"))
        value.clear();
    const QString invalid = ConfigKeys::validate(key, value);
    if (!invalid.isEmpty())
        return {false, invalid, {}, {}};
    if (key.section == QLatin1String("projects"))
        value = ConfigKeys::paletteSlot(value); // color4 -> blue; "" stays ""
    if (key.name == QLatin1String("engine.compare")) {
        const QStringList ids = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (ids.size() == 2)
            value = ids.at(0).trimmed() + QStringLiteral(", ") + ids.at(1).trimmed();
    }

    ConfigFile file;
    if (value.isEmpty())
        file.unset(key.section, key.key);
    else
        file.set(key.section, key.key, value);
    QString error;
    if (!file.save(&error))
        return {false, error, {}, {}};
    const QJsonObject e = entry(key, file, AppConfig::load(file.path()));
    return {true, {}, entryLine(e), e};
}

ActionResult Actions::engines() {
    const AppConfig config = AppConfig::load();
    struct Info { QString id, tool; };
    const QList<Info> list = {{QStringLiteral("vibe"), QStringLiteral("vibe")},
                              {QStringLiteral("claude"), QStringLiteral("claude")},
                              {QStringLiteral("local"), QString()},
                              {QStringLiteral("command"), QString()}};
    QJsonArray array;
    QStringList lines;
    for (const Info &info : list) {
        QJsonObject o{{QStringLiteral("id"), info.id}, {QStringLiteral("default"), config.engine == info.id}};
        QString error;
        auto engine = createEngine(info.id, {}, {}, &error);
        o.insert(QStringLiteral("name"), engine ? engine->displayName() : info.id);
        QString tool = info.tool;
        if (info.id == QLatin1String("command") && !config.command.isEmpty())
            tool = QProcess::splitCommand(config.command).value(0);
        QString path = tool.isEmpty() ? QString() : QStandardPaths::findExecutable(tool);
        bool ready = true;
        QString problem;
        if (info.id == QLatin1String("vibe") || info.id == QLatin1String("claude")) {
            o.insert(QStringLiteral("model"), info.id == QLatin1String("vibe") ? config.vibeModel : config.claudeModel);
            ready = !path.isEmpty();
            if (!ready)
                problem = tr("'%1' was not found on PATH").arg(tool);
        } else if (info.id == QLatin1String("local")) {
            o.insert(QStringLiteral("api"), config.localApi);
            o.insert(QStringLiteral("endpoint"), config.localEndpoint);
            o.insert(QStringLiteral("model"), config.localModel);
            ready = !config.localEndpoint.isEmpty() && !config.localModel.isEmpty();
            if (!ready)
                problem = tr("needs an endpoint and a model");
        } else {
            o.insert(QStringLiteral("command"), config.command);
            ready = !config.command.isEmpty() && !path.isEmpty();
            if (config.command.isEmpty())
                problem = tr("needs a command line");
            else if (path.isEmpty())
                problem = tr("'%1' was not found on PATH").arg(tool);
        }
        if (!tool.isEmpty()) {
            o.insert(QStringLiteral("tool"), tool);
            o.insert(QStringLiteral("path"), path);
        }
        o.insert(QStringLiteral("ready"), ready);
        if (!problem.isEmpty())
            o.insert(QStringLiteral("problem"), problem);
        array.append(o);

        QString line = info.id + (o.value("default").toBool() ? QStringLiteral(" *") : QString()) + QStringLiteral("  ")
                       + (ready ? tr("ready") : problem);
        const QString model = o.value("model").toString();
        if (o.contains("model"))
            line += QStringLiteral("  ") + tr("model: %1").arg(model.isEmpty() ? tr("the tool's default") : model);
        if (!o.value("endpoint").toString().isEmpty())
            line += QStringLiteral("  ") + o.value("endpoint").toString();
        lines << line;
    }
    QJsonObject payload{{QStringLiteral("default"), config.engine},
                        {QStringLiteral("compare"), QJsonArray{config.compare.value(0), config.compare.value(1)}},
                        {QStringLiteral("timeout"), config.timeoutSeconds},
                        {QStringLiteral("engines"), array}};
    return {true, {}, lines.join(QLatin1Char('\n')), payload};
}

QJsonObject Actions::guiConfigSet(const QString &key, const QString &value) {
    const ActionResult r = configSet(key, value);
    return {{QStringLiteral("ok"), r.ok}, {QStringLiteral("error"), r.error}, {QStringLiteral("text"), r.text},
            {QStringLiteral("payload"), r.payload}};
}

QJsonObject Actions::guiEngines() const { return engines().payload; }

void Actions::guiTestEngine(const QString &id) {
    // One test per engine: pressing Test again restarts that engine's test.
    if (auto running = m_tests.find(id); running != m_tests.end()) {
        running->second->disconnect(this);
        running->second->cancel();
        m_tests.erase(running);
    }
    QString error;
    std::unique_ptr<AiEngine> engine = createEngine(id, {}, {}, &error);
    if (!engine) {
        emit engineTested(id, false, error, 0);
        return;
    }
    QString whyNot;
    if (!engine->available(&whyNot)) {
        emit engineTested(id, false, whyNot, 0);
        return;
    }
    // A short request with a known answer: one task tomorrow at 10.
    const QDate today = QDateTime::currentDateTime().date();
    PromptContext context;
    context.today = today;
    context.language = UiLanguage::current();
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    AiEngine *test = engine.get();
    connect(test, &AiEngine::finished, this, [this, id, timer](const Proposal &p, const QString &) {
        const QString first = p.tasks.isEmpty() ? QString() : p.tasks.first().title;
        emit engineTested(id, true,
                          p.tasks.isEmpty() ? tr("Answered, but proposed no tasks.")
                                            : tr("Answered with %n task(s): %1", nullptr, int(p.tasks.size())).arg(first),
                          int(timer->elapsed()));
    });
    connect(test, &AiEngine::failed, this, [this, id, timer](const QString &message) {
        emit engineTested(id, false, message, int(timer->elapsed()));
    });
    m_tests[id] = std::move(engine);
    test->propose(annotateDates(QStringLiteral("Test: write the blog post tomorrow at 10"), today), context);
}

// ai-eval: runs one AI engine against the 20 fixture inputs and prints the
// share of valid and correct answers. This is a manual evaluation tool -- it
// DOES call a real engine; run it by hand, never from the unit tests.
//
// Usage:
//   ai-eval <vibe|claude|ollama|command> [--command "..."] [--model <name>] [--timeout <ms>]
//
// "Correct" is deliberately simple: the engine's answer is valid JSON per
// the schema AND it contains at least one task whose title equals an expected
// task's title (case-insensitive) and whose start date (date part only)
// matches that expected task's start date (tasks without a start must have
// none on both sides). Cases whose expected answer has no tasks at all (only
// "uafklaret") count as correct when the engine also produced no tasks.
#include "engine.h"
#include "prompt.h"
#include "proposal.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

static QString fixturesDir()
{
    return QStringLiteral(FIXTURES_DIR);
}

static QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

static bool runEngine(AiEngine &engine, const QString &text, const PromptContext &ctx,
                      Proposal *proposalOut, QString *errorOut, int guardMs)
{
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    bool done = false;
    bool ok = false;
    QObject::connect(&engine, &AiEngine::finished, [&](const Proposal &p, const QString &) {
        ok = true;
        done = true;
        if (proposalOut)
            *proposalOut = p;
        loop.quit();
    });
    QObject::connect(&engine, &AiEngine::failed, [&](const QString &message) {
        done = true;
        if (errorOut)
            *errorOut = message;
        loop.quit();
    });
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(guardMs);
    engine.propose(text, ctx);
    if (!done)
        loop.exec();
    return ok;
}

// See the comment at the top for the definition of "correct".
static bool isCorrect(const Proposal &actual, const Proposal &expected)
{
    if (expected.tasks.isEmpty())
        return actual.tasks.isEmpty();
    for (const ProposedTask &et : expected.tasks) {
        for (const ProposedTask &at : actual.tasks) {
            if (at.title.trimmed().compare(et.title.trimmed(), Qt::CaseInsensitive) != 0)
                continue;
            const QDate ad = at.start.isValid() ? at.start.date() : QDate();
            const QDate ed = et.start.isValid() ? et.start.date() : QDate();
            if (ad == ed)
                return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        QTextStream(stderr) << "Usage: ai-eval <vibe|claude|ollama|command> [--command \"...\"] "
                               "[--model <name>] [--timeout <ms>]\n";
        return 2;
    }
    const QString engineId = args.at(1);
    QString commandLine;
    QString model;
    int timeoutMs = AiEngine::defaultTimeoutMs;
    for (int i = 2; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--command") && i + 1 < args.size())
            commandLine = args.at(++i);
        else if (args.at(i) == QLatin1String("--model") && i + 1 < args.size())
            model = args.at(++i);
        else if (args.at(i) == QLatin1String("--timeout") && i + 1 < args.size())
            timeoutMs = args.at(++i).toInt();
    }

    std::unique_ptr<AiEngine> engine;
    if (engineId == QLatin1String("vibe"))
        engine = createVibeEngine();
    else if (engineId == QLatin1String("claude"))
        engine = createClaudeEngine();
    else if (engineId == QLatin1String("ollama"))
        engine = createOllamaEngine(model);
    else if (engineId == QLatin1String("command")) {
        if (commandLine.isEmpty()) {
            QTextStream(stderr) << "The command engine needs --command \"...\"\n";
            return 2;
        }
        engine = createCommandEngine(commandLine);
    } else {
        QTextStream(stderr) << "Unknown engine: " << engineId << "\n";
        return 2;
    }
    engine->setTimeoutMs(timeoutMs);

    QString whyNot;
    if (!engine->available(&whyNot)) {
        QTextStream(stderr) << "Engine not available: " << whyNot << "\n";
        return 1;
    }

    const QJsonObject meta =
        QJsonDocument::fromJson(readFile(fixturesDir() + QStringLiteral("/meta.json")).toUtf8()).object();
    PromptContext ctx;
    ctx.today = QDate::fromString(meta.value(QStringLiteral("today")).toString(), Qt::ISODate);
    ctx.language = meta.value(QStringLiteral("language")).toString();

    QDir casesDir(fixturesDir() + QStringLiteral("/cases"));
    const QStringList inputs = casesDir.entryList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name);

    QTextStream out(stdout);
    out << "engine: " << engine->id() << "  today: " << ctx.today.toString(Qt::ISODate)
        << "  cases: " << inputs.size() << "\n\n";

    int valid = 0;
    int correct = 0;
    for (const QString &name : inputs) {
        const QString base = name.left(name.size() - 4);
        const QString input = readFile(casesDir.filePath(name)).trimmed();
        const QString expectedRaw = readFile(casesDir.filePath(base + QStringLiteral(".expected.json")));
        const ParseResult expected = parseProposal(expectedRaw);

        Proposal actual;
        QString error;
        const bool ok = runEngine(*engine, input, ctx, &actual, &error, timeoutMs + 10000);
        if (ok)
            ++valid;
        const bool isCorr = ok && expected.ok && isCorrect(actual, expected.proposal);
        if (isCorr)
            ++correct;
        out << base << (ok ? " valid  " : " INVALID") << (isCorr ? " correct" : "        ") << "  in: \""
            << input.left(50) << "\"";
        if (!ok)
            out << "\n    error: " << error.left(300).replace(QLatin1Char('\n'), QLatin1Char(' '));
        out << "\n";
        out.flush();
    }

    const int total = inputs.size();
    out << "\nSummary: valid " << valid << "/" << total << " (" << (total ? 100.0 * valid / total : 0)
        << "%), correct " << correct << "/" << total << " (" << (total ? 100.0 * correct / total : 0) << "%)\n";
    return 0;
}

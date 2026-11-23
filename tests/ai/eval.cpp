// ai-eval: runs one AI engine against the 20 fixture inputs and prints the
// share of valid and correct answers. This is a manual evaluation tool -- it
// DOES call a real engine; run it by hand, never from the unit tests.
//
// Usage:
//   ai-eval <vibe|claude|lokal|command> [--command "..."] [--model <name>] [--endpoint <url>] [--timeout <ms>]
//
// "lokal" reads endpoint/model/api from ~/.config/omaplan/config.toml
// (OMAPLAN_CONFIG overrides), unless --model/--endpoint are given. Every input
// is date-annotated in C++ first (annotateDates), exactly like `omaplan add`,
// so the engine sees the resolved dates in the text.
//
// "Correct" is deliberately simple: the engine's answer is valid JSON per
// the schema AND it contains at least one task whose title equals an expected
// task's title (case-insensitive) and whose start date (date part only)
// matches that expected task's start date (tasks without a start must have
// none on both sides). Cases whose expected answer has no tasks at all (only
// "uafklaret") count as correct when the engine also produced no tasks.
#include "dateannotate.h"
#include "engine.h"
#include "prompt.h"
#include "proposal.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
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

// Title match, deliberately lenient: exact (case-insensitive), or one title
// contains the stem of the other's first word ("Vand planterne" ~ "Vande
// planterne": "vande" -> stem "vand").
static bool titlesMatch(const QString &a, const QString &b)
{
    const QString x = a.trimmed().toLower();
    const QString y = b.trimmed().toLower();
    if (x == y)
        return true;
    static const QRegularExpression wordSplit(QStringLiteral("\\s+"));
    const QString wx = x.split(wordSplit).value(0);
    const QString wy = y.split(wordSplit).value(0);
    if (wx.isEmpty() || wy.isEmpty())
        return false;
    const QString sx = wx.size() > 4 ? wx.left(wx.size() - 1) : wx;
    const QString sy = wy.size() > 4 ? wy.left(wy.size() - 1) : wy;
    return x.contains(sy) || y.contains(sx);
}

static bool datePartMatches(const ProposedTask &at, const ProposedTask &et)
{
    const QDate ad = at.start.isValid() ? at.start.date() : QDate();
    const QDate ed = et.start.isValid() ? et.start.date() : QDate();
    return ad == ed;
}

// See the comment at the top for the definition of "correct".
static bool isCorrect(const Proposal &actual, const Proposal &expected)
{
    if (expected.tasks.isEmpty())
        return actual.tasks.isEmpty();
    for (const ProposedTask &et : expected.tasks) {
        for (const ProposedTask &at : actual.tasks) {
            if (!titlesMatch(at.title, et.title))
                continue;
            if (datePartMatches(at, et))
                return true;
        }
    }
    return false;
}

// Date correctness counted separately from the title: at least one actual task
// per expected task with the same start date (date part only).
static bool isDateCorrect(const Proposal &actual, const Proposal &expected)
{
    if (expected.tasks.isEmpty())
        return actual.tasks.isEmpty();
    for (const ProposedTask &et : expected.tasks) {
        bool found = false;
        for (const ProposedTask &at : actual.tasks) {
            if (datePartMatches(at, et)) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        QTextStream(stderr) << "Usage: ai-eval <vibe|claude|lokal|command> [--command \"...\"] "
                               "[--model <name>] [--endpoint <url>] [--timeout <ms>]\n";
        return 2;
    }
    const QString engineId = args.at(1);
    QString commandLine;
    QString model;
    QString endpoint;
    int timeoutMs = AiEngine::defaultTimeoutMs;
    for (int i = 2; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--command") && i + 1 < args.size())
            commandLine = args.at(++i);
        else if (args.at(i) == QLatin1String("--model") && i + 1 < args.size())
            model = args.at(++i);
        else if (args.at(i) == QLatin1String("--endpoint") && i + 1 < args.size())
            endpoint = args.at(++i);
        else if (args.at(i) == QLatin1String("--timeout") && i + 1 < args.size())
            timeoutMs = args.at(++i).toInt();
    }

    // Same factory as `omaplan add`: lokal reads config.toml, CLI flags win.
    // The command engine takes an explicit --command for ad-hoc testing.
    std::unique_ptr<AiEngine> engine;
    if (engineId == QLatin1String("command") && !commandLine.isEmpty())
        engine = createCommandEngine(commandLine);
    else {
        QString error;
        engine = createEngine(engineId, model, endpoint, &error);
        if (!engine) {
            QTextStream(stderr) << error << "\n";
            return 2;
        }
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
    int dateCorrect = 0;
    for (const QString &name : inputs) {
        const QString base = name.left(name.size() - 4);
        const QString input = readFile(casesDir.filePath(name)).trimmed();
        const QString expectedRaw = readFile(casesDir.filePath(base + QStringLiteral(".expected.json")));
        const ParseResult expected = parseProposal(expectedRaw);

        // Same path as `omaplan add`: annotate dates in C++ first, then call
        // the engine (which builds the stable system prompt + user prompt).
        const QString annotated = annotateDates(input, ctx.today);

        Proposal actual;
        QString error;
        const bool ok = runEngine(*engine, annotated, ctx, &actual, &error, timeoutMs + 10000);
        if (ok)
            ++valid;
        const bool isCorr = ok && expected.ok && isCorrect(actual, expected.proposal);
        if (isCorr)
            ++correct;
        const bool isDateCorr = ok && expected.ok && isDateCorrect(actual, expected.proposal);
        if (isDateCorr)
            ++dateCorrect;
        out << base << (ok ? " valid  " : " INVALID")
            << (isCorr ? " correct" : isDateCorr ? " date-ok" : "        ") << "  in: \""
            << input.left(50) << "\"";
        if (annotated != input)
            out << "\n    annotated: \"" << annotated.left(80) << "\"";
        if (!ok)
            // Full error text (including the raw response) -- do not truncate,
            // otherwise the cause is invisible.
            out << "\n    error: " << error.replace(QLatin1Char('\n'), QLatin1Char(' '));
        else if (!isCorr) {
            // A valid answer that missed: show both, or the miss cannot be told
            // apart from a too strict "correct" (eval-vibe-4, cases 02/12/14).
            out << "\n    got:      " << QJsonDocument(actual.toJson()).toJson(QJsonDocument::Compact);
            if (expected.ok)
                out << "\n    expected: " << QJsonDocument(expected.proposal.toJson()).toJson(QJsonDocument::Compact);
        }
        out << "\n";
        out.flush();
    }

    const int total = inputs.size();
    out << "\nSummary: valid " << valid << "/" << total << " (" << (total ? 100.0 * valid / total : 0)
        << "%), correct " << correct << "/" << total << " (" << (total ? 100.0 * correct / total : 0)
        << "%), date-correct " << dateCorrect << "/" << total << " ("
        << (total ? 100.0 * dateCorrect / total : 0) << "%)\n";
    return 0;
}

// Unit tests for the AI layer. No test calls a real AI: engines are either
// a FakeEngine subclass or a CommandEngine running cat/printf/sleep.
#include "engine.h"
#include "prompt.h"
#include "proposal.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTimer>

static QString fixturesDir()
{
    return QStringLiteral(FIXTURES_DIR);
}

static QString readFixture(const QString &relativePath)
{
    QFile f(fixturesDir() + QLatin1Char('/') + relativePath);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// Runs an engine synchronously. Returns true when finished() was emitted.
static bool runEngine(AiEngine &engine, const QString &text, Proposal *proposalOut, QString *errorOut,
                      int guardMs = 10000)
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
    PromptContext ctx;
    ctx.today = QDate(2026, 11, 23); // the fixed fixture date (a Monday)
    ctx.language = QStringLiteral("da");
    engine.propose(text, ctx);
    if (!done) // process-based engines answer asynchronously
        loop.exec();
    return ok;
}

// An engine that answers from a preset raw response without any subprocess.
class FakeEngine : public AiEngine {
public:
    explicit FakeEngine(QObject *parent = nullptr) : AiEngine(parent) {}
    QString id() const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }

    QByteArray response;
    QString failMessage;
    bool hang = false; // never answers -> exercises the timeout
    QString lastSystemPrompt;
    QString lastUserPrompt;

protected:
    void start(const QString &systemPrompt, const QString &userPrompt) override
    {
        lastSystemPrompt = systemPrompt;
        lastUserPrompt = userPrompt;
        if (hang)
            return;
        if (!failMessage.isEmpty()) {
            fail(failMessage);
            return;
        }
        finishWithRaw(QString::fromUtf8(response));
    }
};

class TestPrompt : public QObject {
    Q_OBJECT
private slots:
    void systemPromptContainsDateWeekdayTimezoneLanguageSchema()
    {
        PromptContext ctx;
        ctx.today = QDate(2026, 11, 23); // Monday
        ctx.language = QStringLiteral("da");
        ctx.existingTasks = {QStringLiteral("2026-11-24 09:00 Research (Blog)")};
        const QString prompt = buildSystemPrompt(ctx);
        QVERIFY(prompt.contains(QStringLiteral("2026-11-23")));
        QVERIFY(prompt.contains(QStringLiteral("Monday")));
        QVERIFY(prompt.contains(QStringLiteral("mandag"), Qt::CaseInsensitive));
        QVERIFY(prompt.contains(QStringLiteral("Europe/Copenhagen")));
        QVERIFY(prompt.contains(QStringLiteral("\"da\"")) || prompt.contains(QStringLiteral(" da")));
        QVERIFY(prompt.contains(QStringLiteral("own instruction"))); // app's own system prompt, not user text
        QVERIFY(prompt.contains(QStringLiteral("opgaver")));   // the schema is embedded
        QVERIFY(prompt.contains(QStringLiteral("gaet")));       // guessing rules
        QVERIFY(prompt.contains(QStringLiteral("uafklaret")));
        QVERIFY(prompt.contains(QStringLiteral("2026-11-24 09:00 Research (Blog)")));
        QVERIFY(prompt.contains(QStringLiteral("på tirsdag"))); // Danish date expressions mentioned
    }

    void userPromptIsTheRawInput()
    {
        QCOMPARE(buildUserPrompt(QStringLiteral("  køb mælk\n")), QStringLiteral("køb mælk"));
        PromptContext ctx;
        ctx.today = QDate(2026, 11, 23);
        const QString full = buildFullPrompt(ctx, QStringLiteral("køb mælk"));
        QVERIFY(full.contains(buildSystemPrompt(ctx)));
        QVERIFY(full.endsWith(QStringLiteral("køb mælk")));
    }
};

class TestProposal : public QObject {
    Q_OBJECT
private slots:
    void validResponseParses()
    {
        const ParseResult r = parseProposal(QStringLiteral(
            R"({"opgaver": [{"titel": "Køb mælk", "noter": "gerne øko", "projekt": "Indkøb",)"
            R"("prioritet": 2, "start": "2026-11-24", "varighed_min": 30, "frist": "2026-11-27T12:00",)"
            R"("gaet": ["projekt", "varighed_min"]}], "uafklaret": ["??"]})"));
        QVERIFY(r.ok);
        QCOMPARE(r.proposal.tasks.size(), 1);
        const ProposedTask &t = r.proposal.tasks.first();
        QCOMPARE(t.title, QStringLiteral("Køb mælk"));
        QCOMPARE(t.notes, QStringLiteral("gerne øko"));
        QCOMPARE(t.project, QStringLiteral("Indkøb"));
        QCOMPARE(t.priority, 2);
        QCOMPARE(t.durationMin, 30);
        QVERIFY(t.start.isValid());
        QCOMPARE(t.start.date(), QDate(2026, 11, 24));
        QVERIFY(!t.startHasTime); // date only
        QVERIFY(t.deadline.isValid());
        QVERIFY(t.deadlineHasTime); // date+time
        QCOMPARE(t.guessed, QStringList({QStringLiteral("projekt"), QStringLiteral("varighed_min")}));
        QCOMPARE(r.proposal.unclear, QStringList({QStringLiteral("??")}));
    }

    void dateOnlyAndDateTime()
    {
        const ParseResult r = parseProposal(QStringLiteral(
            R"({"opgaver": [{"titel": "A", "start": "2026-11-24T10:00"}, {"titel": "B", "start": "2026-11-25"}]})"));
        QVERIFY(r.ok);
        QVERIFY(r.proposal.tasks[0].startHasTime);
        QCOMPARE(r.proposal.tasks[0].start.time(), QTime(10, 0));
        QVERIFY(!r.proposal.tasks[1].startHasTime);
    }

    void schemaViolationsAreRejectedWithRawResponse()
    {
        const QString missingTitle = QStringLiteral(R"({"opgaver": [{"start": "2026-11-24"}]})");
        QVERIFY(!parseProposal(missingTitle).ok);
        QVERIFY(parseProposal(missingTitle).error.contains(missingTitle));

        const QString emptyTitle = QStringLiteral(R"({"opgaver": [{"titel": ""}]})");
        QVERIFY(!parseProposal(emptyTitle).ok);

        const QString extraField = QStringLiteral(R"({"opgaver": [{"titel": "X", "ukendt": 1}]})");
        QVERIFY(!parseProposal(extraField).ok);

        const QString badPriority = QStringLiteral(R"({"opgaver": [{"titel": "X", "prioritet": 4}]})");
        QVERIFY(!parseProposal(badPriority).ok);

        const QString badGaet = QStringLiteral(R"({"opgaver": [{"titel": "X", "gaet": ["titel"]}]})");
        QVERIFY(!parseProposal(badGaet).ok);

        const QString missingOpgaver = QStringLiteral(R"({"foo": 1})");
        QVERIFY(!parseProposal(missingOpgaver).ok);
    }

    void invalidDatesAreRejected()
    {
        const QString raw = QStringLiteral(R"({"opgaver": [{"titel": "X", "start": "ikke en dato"}]})");
        const ParseResult r = parseProposal(raw);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("start")));
        QVERIFY(r.error.contains(raw));
    }

    void emptyResponseIsRejected()
    {
        QVERIFY(!parseProposal(QString()).ok);
        QVERIFY(!parseProposal(QStringLiteral("   \n ")).ok);
    }

    void envelopeFixturesParse()
    {
        const QStringList valid = {QStringLiteral("envelopes/vibe-valid-envelope.json"),
                                   QStringLiteral("envelopes/vibe-valid-direct.json"),
                                   QStringLiteral("envelopes/claude-valid.json"),
                                   QStringLiteral("envelopes/ollama-valid.json"),
                                   QStringLiteral("envelopes/command-valid.json"),
                                   QStringLiteral("envelopes/wrapped-fence.txt"),
                                   QStringLiteral("envelopes/wrapped-prose.txt")};
        for (const QString &rel : valid) {
            const QString raw = readFixture(rel);
            QVERIFY2(!raw.isEmpty(), qPrintable(rel));
            const ParseResult r = parseProposal(raw);
            QVERIFY2(r.ok, qPrintable(rel + QLatin1Char(':') + r.error));
            QVERIFY(!r.proposal.tasks.isEmpty());
            QCOMPARE(r.proposal.tasks.first().title, QStringLiteral("Køb mælk"));
        }
    }

    // Builds a vibe --output json event array without raw string literals
    // (moc misparses those in this file).
    static QJsonObject vibeTextMessage(const QString &role, const QStringList &textParts)
    {
        QJsonArray content;
        for (const QString &part : textParts) {
            QJsonObject t;
            t.insert(QStringLiteral("type"), QStringLiteral("text"));
            t.insert(QStringLiteral("text"), part);
            content.append(t);
        }
        QJsonObject m;
        m.insert(QStringLiteral("type"), QStringLiteral("message"));
        m.insert(QStringLiteral("role"), role);
        m.insert(QStringLiteral("content"), content);
        return m;
    }

    void vibeEventArrayTakesLastAssistantMessage()
    {
        // Two assistant messages: the last one wins, and its text parts are
        // concatenated. Reasoning in between must be ignored.
        QJsonArray events;
        events.append(vibeTextMessage(QStringLiteral("user"), {QStringLiteral("køb mælk")}));
        events.append(vibeTextMessage(QStringLiteral("assistant"),
                                      {QStringLiteral("{\"opgaver\": [{\"titel\": \"Forkert\"}]}")}));
        QJsonObject reasoning;
        reasoning.insert(QStringLiteral("type"), QStringLiteral("reasoning"));
        reasoning.insert(QStringLiteral("text"), QStringLiteral("{\"opgaver\": [{\"titel\": \"Fra reasoning\"}]}"));
        events.append(reasoning);
        events.append(vibeTextMessage(QStringLiteral("assistant"),
                                      {QStringLiteral("{\"opgaver\": ["),
                                       QStringLiteral("{\"titel\": \"Køb mælk\", \"start\": \"2026-11-24\"}]}")}));
        const QString raw = QString::fromUtf8(QJsonDocument(events).toJson(QJsonDocument::Compact));
        const ParseResult r = parseProposal(raw);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.proposal.tasks.size(), 1);
        QCOMPARE(r.proposal.tasks.first().title, QStringLiteral("Køb mælk"));
        QCOMPARE(r.proposal.tasks.first().start.date(), QDate(2026, 11, 24));
    }

    void vibeEventArrayWithoutAssistantMessageFails()
    {
        QJsonArray events;
        events.append(vibeTextMessage(QStringLiteral("user"), {QStringLiteral("køb mælk")}));
        QJsonObject reasoning;
        reasoning.insert(QStringLiteral("type"), QStringLiteral("reasoning"));
        reasoning.insert(QStringLiteral("text"), QStringLiteral("{\"opgaver\": [{\"titel\": \"Kun reasoning\"}]}"));
        events.append(reasoning);
        const QString raw = QString::fromUtf8(QJsonDocument(events).toJson(QJsonDocument::Compact));
        const ParseResult r = parseProposal(raw);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("no assistant message")));
        QVERIFY(r.error.contains(raw.left(40))); // raw response included
    }

    void invalidEnvelopeFixturesAreRejectedWithRaw()
    {
        // vibe-invalid-array.json has valid JSON ONLY in its reasoning entry,
        // so it must be rejected -- this proves reasoning text is never used.
        const QStringList invalid = {QStringLiteral("envelopes/vibe-invalid.json"),
                                     QStringLiteral("envelopes/vibe-invalid-array.json"),
                                     QStringLiteral("envelopes/claude-invalid.json"),
                                     QStringLiteral("envelopes/ollama-invalid.json"),
                                     QStringLiteral("envelopes/command-invalid.json")};
        for (const QString &rel : invalid) {
            const QString raw = readFixture(rel);
            const ParseResult r = parseProposal(raw);
            QVERIFY2(!r.ok, qPrintable(rel));
            QVERIFY2(r.error.contains(raw.trimmed().left(40)), qPrintable(rel + QLatin1Char(':') + r.error));
        }
    }

    void commandValidFixtureFields()
    {
        const ParseResult r = parseProposal(readFixture(QStringLiteral("envelopes/command-valid.json")));
        QVERIFY(r.ok);
        const ProposedTask &t = r.proposal.tasks.first();
        QCOMPARE(t.priority, 2);
        QCOMPARE(t.durationMin, 30);
        QCOMPARE(t.project, QStringLiteral("Indkøb"));
        QVERIFY(t.guessed.contains(QStringLiteral("projekt")));
        QVERIFY(t.guessed.contains(QStringLiteral("varighed_min")));
    }
};

class TestEngine : public QObject {
    Q_OBJECT
private slots:
    void fakeEngineDeliversParsedProposal()
    {
        FakeEngine engine;
        engine.response = R"({"opgaver": [{"titel": "Køb mælk", "start": "2026-11-24"}]})";
        Proposal proposal;
        QString error;
        QVERIFY(runEngine(engine, QStringLiteral("køb mælk i morgen"), &proposal, &error));
        QCOMPARE(error, QString());
        QCOMPARE(proposal.tasks.size(), 1);
        QCOMPARE(proposal.tasks.first().title, QStringLiteral("Køb mælk"));
        // The engine received the built prompts.
        QVERIFY(engine.lastSystemPrompt.contains(QStringLiteral("2026-11-23")));
        QVERIFY(engine.lastSystemPrompt.contains(QStringLiteral("Europe/Copenhagen")));
        QCOMPARE(engine.lastUserPrompt, QStringLiteral("køb mælk i morgen"));
    }

    void fakeEngineInvalidResponseFailsWithRaw()
    {
        FakeEngine engine;
        engine.response = "ikke json overhovedet";
        QString error;
        QVERIFY(!runEngine(engine, QStringLiteral("x"), nullptr, &error));
        QVERIFY(error.contains(QStringLiteral("ikke json overhovedet")));
    }

    void fakeEngineFailMessage()
    {
        FakeEngine engine;
        engine.failMessage = QStringLiteral("something broke");
        QString error;
        QVERIFY(!runEngine(engine, QStringLiteral("x"), nullptr, &error));
        QCOMPARE(error, QStringLiteral("something broke"));
    }

    void timeoutFails()
    {
        FakeEngine engine;
        engine.hang = true;
        engine.setTimeoutMs(150);
        QString error;
        QVERIFY(!runEngine(engine, QStringLiteral("x"), nullptr, &error, 5000));
        QVERIFY2(error.contains(QStringLiteral("Timeout"), Qt::CaseInsensitive), qPrintable(error));
        QVERIFY(!engine.isRunning());
    }

    void cancelStopsRunningEngine()
    {
        FakeEngine engine;
        engine.hang = true;
        engine.setTimeoutMs(60000);
        QString error;
        // cancel() must arrive while the engine is "running"; emulate by
        // calling propose() and then cancel() before the event loop runs.
        QEventLoop loop;
        bool done = false;
        QObject::connect(&engine, &AiEngine::failed, [&](const QString &m) {
            done = true;
            error = m;
            loop.quit();
        });
        PromptContext ctx;
        ctx.today = QDate(2026, 11, 23);
        engine.propose(QStringLiteral("x"), ctx); // hangs, stays running
        QVERIFY(engine.isRunning());
        engine.cancel();
        QVERIFY(!engine.isRunning());
        QVERIFY(done);
        QCOMPARE(error, QStringLiteral("Cancelled."));
    }

    void missingToolGivesClearError()
    {
        auto engine = createCommandEngine(QStringLiteral("definitely-not-a-real-binary-xyz --flag"));
        QString why;
        QVERIFY(!engine->available(&why));
        QVERIFY(why.contains(QStringLiteral("definitely-not-a-real-binary-xyz")));
        QString error;
        QVERIFY(!runEngine(*engine, QStringLiteral("x"), nullptr, &error));
        QVERIFY2(error.contains(QStringLiteral("Could not start")), qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("definitely-not-a-real-binary-xyz")), qPrintable(error));
    }

    void commandEngineWithCatEchoesPromptAndFailsValidation()
    {
        // cat echoes the prompt back; that is not schema JSON, so the engine
        // must fail and include the raw response. cat is not an AI.
        auto engine = createCommandEngine(QStringLiteral("cat"));
        QString error;
        QVERIFY(!runEngine(*engine, QStringLiteral("køb mælk"), nullptr, &error));
        QVERIFY2(error.contains(QStringLiteral("Invalid response")), qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("Raw response")), qPrintable(error));
    }

    void commandEngineWithCatFixtureReturnsValidProposal()
    {
        // cat of a fixture file: no shell quoting needed, and not an AI.
        auto engine = createCommandEngine(QStringLiteral("cat ") + fixturesDir()
                                           + QStringLiteral("/envelopes/command-valid.json"));
        Proposal proposal;
        QString error;
        QVERIFY2(runEngine(*engine, QStringLiteral("køb mælk"), &proposal, &error), qPrintable(error));
        QCOMPARE(proposal.tasks.size(), 1);
        QCOMPARE(proposal.tasks.first().title, QStringLiteral("Køb mælk"));
    }

    void commandEngineWithInvalidFixtureFailsWithRaw()
    {
        auto engine = createCommandEngine(QStringLiteral("cat ") + fixturesDir()
                                           + QStringLiteral("/envelopes/command-invalid.json"));
        QString error;
        QVERIFY(!runEngine(*engine, QStringLiteral("køb mælk"), nullptr, &error));
        QVERIFY2(error.contains(QStringLiteral("Invalid response")), qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("ukendt_felt")), qPrintable(error));
    }

    void commandEngineNonZeroExitReportsStderr()
    {
        auto engine = createCommandEngine(QStringLiteral("sh -c \"echo noget gik galt >&2; exit 3\""));
        QString error;
        QVERIFY(!runEngine(*engine, QStringLiteral("x"), nullptr, &error));
        QVERIFY2(error.contains(QStringLiteral("code 3")), qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("noget gik galt")), qPrintable(error));
    }

    void commandEngineTimeoutKillsProcess()
    {
        auto engine = createCommandEngine(QStringLiteral("sleep 30")); // not an AI
        engine->setTimeoutMs(200);
        QString error;
        QVERIFY(!runEngine(*engine, QStringLiteral("x"), nullptr, &error, 5000));
        QVERIFY2(error.contains(QStringLiteral("Timeout"), Qt::CaseInsensitive), qPrintable(error));
    }

    void emptyCommandLineFails()
    {
        auto engine = createCommandEngine(QString());
        QString error;
        QVERIFY(!runEngine(*engine, QStringLiteral("x"), nullptr, &error));
        QVERIFY2(error.contains(QStringLiteral("No command configured")), qPrintable(error));
    }

    void vibeAndClaudeRequireTheirTools()
    {
        auto vibe = createVibeEngine();
        auto claude = createClaudeEngine();
        QCOMPARE(vibe->id(), QStringLiteral("vibe"));
        QCOMPARE(claude->id(), QStringLiteral("claude"));
        // available() must not crash and must name the tool when missing.
        QString why;
        const bool vibeOk = vibe->available(&why);
        if (!vibeOk)
            QVERIFY(why.contains(QStringLiteral("vibe")));
        const bool claudeOk = claude->available(&why);
        if (!claudeOk)
            QVERIFY(why.contains(QStringLiteral("claude")));
    }

    void isolatedWorkingDirectoryIsEmptyTempDir()
    {
        // A process engine that runs `pwd` in the isolated working directory.
        // pwd is not an AI; its stdout lands in the raw response of the error.
        class PwdEngine : public AiEngine {
        public:
            QString id() const override { return QStringLiteral("pwd"); }
            QString displayName() const override { return QStringLiteral("Pwd"); }
        protected:
            void start(const QString &, const QString &) override
            {
                launch(QStringLiteral("pwd"), {}, QByteArray(), QStringLiteral("pwd missing"),
                       useIsolatedWorkingDirectory());
            }
        } engine;
        QString error;
        QVERIFY(!runEngine(engine, QStringLiteral("x"), nullptr, &error));
        const QString wd = engine.lastLaunchWorkingDirectory();
        QVERIFY2(wd.startsWith(QDir::tempPath()), qPrintable(wd));
        QVERIFY2(error.contains(wd), qPrintable(error)); // pwd printed the isolated dir
        QDir dir(wd);
        QVERIFY2(dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty(),
                 qPrintable(wd + QStringLiteral(" is not empty")));
    }
};

class TestFixtures : public QObject {
    Q_OBJECT
private slots:
    void metaHasFixedToday()
    {
        const QJsonObject meta = QJsonDocument::fromJson(readFixture(QStringLiteral("meta.json")).toUtf8()).object();
        QCOMPARE(meta.value(QStringLiteral("today")).toString(), QStringLiteral("2026-11-23"));
        QCOMPARE(meta.value(QStringLiteral("timezone")).toString(), QStringLiteral("Europe/Copenhagen"));
        QCOMPARE(QDate(2026, 11, 23).dayOfWeek(), 1); // Monday
    }

    void allTwentyCasesHaveValidExpectedJson()
    {
        QDir dir(fixturesDir() + QStringLiteral("/cases"));
        const QStringList inputs = dir.entryList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name);
        QCOMPARE(inputs.size(), 20);
        for (const QString &name : inputs) {
            const QString base = name.left(name.size() - 4);
            const QString input = readFixture(QStringLiteral("cases/") + name);
            const QString expectedRaw = readFixture(QStringLiteral("cases/") + base + QStringLiteral(".expected.json"));
            QVERIFY2(!input.trimmed().isEmpty(), qPrintable(name));
            const ParseResult r = parseProposal(expectedRaw);
            QVERIFY2(r.ok, qPrintable(base + QLatin1Char(':') + r.error));
        }
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int status = 0;
    {
        TestPrompt t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestProposal t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestEngine t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestFixtures t;
        status |= QTest::qExec(&t, argc, argv);
    }
    return status;
}

#include "tst_ai.moc"

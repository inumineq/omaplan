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
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
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

// A minimal schema-valid proposal as compact JSON, for fake engines to emit.
static QByteArray proposalJson(const QString &title)
{
    const QJsonObject task{
        {QStringLiteral("titel"), title},
        {QStringLiteral("start"), QStringLiteral("2026-11-24")},
    };
    const QJsonObject root{{QStringLiteral("opgaver"), QJsonArray{task}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// Writes an executable fake CLI tool (a shell script) into dir.
static bool writeFakeTool(const QString &dir, const QString &name, const QString &scriptBody)
{
    const QString path = QDir(dir).filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(scriptBody.toUtf8());
    f.close();
    return QFile::setPermissions(path,
                                 QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                     | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                     | QFileDevice::ReadOther | QFileDevice::ExeOther);
}

// Minimal fake HTTP server for the local engine: answers every request with a
// canned status/body and records the request line and body. No real AI, no
// real server -- loopback only.
struct FakeHttpServer {
    QTcpServer server;
    QByteArray incoming; // one request is expected per test
    QByteArray requestLine;
    QByteArray requestBody;
    QByteArray responseBody;
    int responseStatus = 200;
    int requests = 0;

    bool listen()
    {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            QTcpSocket *sock = server.nextPendingConnection();
            QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock] {
                incoming += sock->readAll();
                const int headerEnd = incoming.indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                int contentLength = 0;
                const QByteArray headers = incoming.left(headerEnd);
                for (const QByteArray &line : headers.split('\n')) {
                    const QByteArray l = line.trimmed().toLower();
                    if (l.startsWith("content-length:"))
                        contentLength = l.mid(15).trimmed().toInt();
                }
                if (incoming.size() < headerEnd + 4 + contentLength)
                    return; // body not complete yet
                requestLine = headers.left(headers.indexOf("\r\n"));
                requestBody = incoming.mid(headerEnd + 4, contentLength);
                ++requests;
                const QByteArray reason = responseStatus == 200 ? QByteArrayLiteral("OK")
                                                                : QByteArrayLiteral("Server Error");
                sock->write("HTTP/1.1 " + QByteArray::number(responseStatus) + " " + reason
                            + "\r\nContent-Type: application/json\r\nContent-Length: "
                            + QByteArray::number(responseBody.size()) + "\r\nConnection: close\r\n\r\n"
                            + responseBody);
                sock->flush();
                sock->disconnectFromHost();
            });
        });
        return server.listen(QHostAddress::LocalHost, 0);
    }

    QString endpoint() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
};

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
        QVERIFY(prompt.contains(QStringLiteral("på tirsdag"))); // Danish date expressions mentioned
        // eval-vibe-2: engines must not wander into prose or tool calls.
        QVERIFY(prompt.contains(QStringLiteral("no tool calls"), Qt::CaseInsensitive));
        QVERIFY(prompt.contains(QStringLiteral("only the JSON object"), Qt::CaseInsensitive));
        // Existing tasks moved to the user prompt: the system prompt is
        // identical all day, so engines can cache it.
        QVERIFY(!prompt.contains(QStringLiteral("Research (Blog)")));
        PromptContext ctx2 = ctx;
        ctx2.existingTasks.clear();
        QCOMPARE(buildSystemPrompt(ctx2), prompt);
    }

    void userPromptIsTheRawInput()
    {
        QCOMPARE(buildUserPrompt(QStringLiteral("  køb mælk\n")), QStringLiteral("køb mælk"));
        // Existing tasks are part of the user prompt, after the raw input.
        const QString withTasks = buildUserPrompt(QStringLiteral("køb mælk"),
                                                  {QStringLiteral("2026-11-24 09:00 Research (Blog)")});
        QVERIFY(withTasks.contains(QStringLiteral("køb mælk")));
        QVERIFY(withTasks.contains(QStringLiteral("2026-11-24 09:00 Research (Blog)")));
        QVERIFY(withTasks.indexOf(QStringLiteral("køb mælk")) < withTasks.indexOf(QStringLiteral("Research (Blog)")));
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

    void maxItemsAreEnforced()
    {
        // docs/ai-skema.json: opgaver max 10, gaet max 5, uafklaret max 10.
        QJsonArray tasks;
        for (int i = 0; i < 11; ++i)
            tasks.append(QJsonObject{{QStringLiteral("titel"), QStringLiteral("Opgave %1").arg(i)}});
        const QString tooManyTasks = QString::fromUtf8(
            QJsonDocument(QJsonObject{{QStringLiteral("opgaver"), tasks}}).toJson(QJsonDocument::Compact));
        const ParseResult r = parseProposal(tooManyTasks);
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains(QStringLiteral("maxItems")), qPrintable(r.error));

        // Boundary: exactly 10 tasks is fine.
        tasks.removeLast();
        const QString tenTasks = QString::fromUtf8(
            QJsonDocument(QJsonObject{{QStringLiteral("opgaver"), tasks}}).toJson(QJsonDocument::Compact));
        QVERIFY2(parseProposal(tenTasks).ok, qPrintable(parseProposal(tenTasks).error));

        QJsonArray gaet;
        for (const char *key : {"start", "varighed_min", "frist", "projekt", "prioritet", "start"})
            gaet.append(QString::fromLatin1(key));
        const QString tooMuchGaet = QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("opgaver"), QJsonArray{QJsonObject{{QStringLiteral("titel"), QStringLiteral("X")},
                                                                {QStringLiteral("gaet"), gaet}}}}}).toJson(QJsonDocument::Compact));
        const ParseResult rg = parseProposal(tooMuchGaet);
        QVERIFY(!rg.ok);
        QVERIFY2(rg.error.contains(QStringLiteral("maxItems")), qPrintable(rg.error));

        QJsonArray unclear;
        for (int i = 0; i < 11; ++i)
            unclear.append(QStringLiteral("punkt %1").arg(i));
        const QString tooManyUnclear = QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("opgaver"), QJsonArray{}},
            {QStringLiteral("uafklaret"), unclear}}).toJson(QJsonDocument::Compact));
        QVERIFY(!parseProposal(tooManyUnclear).ok);
    }

    void schemaEchoAtTopLevelIsIgnored()
    {
        // Some engines echo the schema's own metadata ($schema, title,
        // description, type) at the top level; it must be ignored when a
        // valid "opgaver" array is present (eval-vibe-2, case 09).
        const QString raw = QStringLiteral(
            R"({"$schema": "https://json-schema.org/draft/2020-12/schema",)"
            R"("title": "Forslag", "description": "Svar", "type": "object",)"
            R"("opgaver": [{"titel": "Køb mælk", "start": "2026-11-24"}], "uafklaret": []})");
        const ParseResult r = parseProposal(raw);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.proposal.tasks.size(), 1);
        QCOMPARE(r.proposal.tasks.first().title, QStringLiteral("Køb mælk"));
        // ...but other unknown top-level keys are still rejected.
        const QString extra = QStringLiteral(
            R"({"opgaver": [{"titel": "X"}], "ukendt_ting": 1})");
        QVERIFY(!parseProposal(extra).ok);
    }

    void datePatternIsEnforced()
    {
        // pattern on start/frist: ^\d{4}-\d{2}-\d{2}(T\d{2}:\d{2})?$
        const QString withSeconds = QStringLiteral(R"({"opgaver": [{"titel": "X", "start": "2026-11-24T10:00:00"}]})");
        QVERIFY(!parseProposal(withSeconds).ok);
        const QString spaceSeparated = QStringLiteral(R"({"opgaver": [{"titel": "X", "start": "2026-11-24 10:00"}]})");
        QVERIFY(!parseProposal(spaceSeparated).ok);
        const QString danishOrder = QStringLiteral(R"({"opgaver": [{"titel": "X", "frist": "24-11-2026"}]})");
        QVERIFY(!parseProposal(danishOrder).ok);
        const QString badMonth = QStringLiteral(R"({"opgaver": [{"titel": "X", "start": "2026-13-24"}]})");
        QVERIFY(!parseProposal(badMonth).ok);
        // The two valid forms still pass.
        QVERIFY(parseProposal(QStringLiteral(R"({"opgaver": [{"titel": "X", "start": "2026-11-24"}]})")).ok);
        QVERIFY(parseProposal(QStringLiteral(R"({"opgaver": [{"titel": "X", "frist": "2026-11-24T10:00"}]})")).ok);
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

    // Regression: launch() calls kill() right before starting the process, and
    // kill() used to delete the system prompt file -- so the vibe process read
    // a nonexistent file. The fake vibe below records whether the file
    // existed when the process read it.
    void vibeEngineSystemPromptFileSurvivesLaunch()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Canned vibe --output json event array with one assistant message.
        QJsonArray content;
        content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                   {QStringLiteral("text"), QString::fromUtf8(proposalJson(QStringLiteral("Køb mælk")))}});
        QJsonArray events;
        events.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("message")},
                                  {QStringLiteral("role"), QStringLiteral("assistant")},
                                  {QStringLiteral("content"), content}});
        QFile resp(dir.filePath(QStringLiteral("response.json")));
        QVERIFY(resp.open(QIODevice::WriteOnly));
        resp.write(QJsonDocument(events).toJson(QJsonDocument::Compact));
        resp.close();

        const QString script = QStringLiteral(
            "#!/bin/sh\n"
            "dir=\"%1\"\n"
            "printf '%s\\n' \"$@\" > \"$dir/args.txt\"\n"
            "prev=\n"
            "for a in \"$@\"; do\n"
            "  if [ \"$prev\" = \"--system-prompt-file\" ]; then f=\"$a\"; fi\n"
            "  prev=\"$a\"\n"
            "done\n"
            "if [ -f \"$f\" ]; then\n"
            "  echo existed > \"$dir/marker\"\n"
            "  cp \"$f\" \"$dir/system-prompt.txt\"\n"
            "else\n"
            "  echo missing > \"$dir/marker\"\n"
            "fi\n"
            "cat \"$dir/response.json\"\n").arg(dir.path());
        QVERIFY(writeFakeTool(dir.path(), QStringLiteral("vibe"), script));

        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", dir.path().toLocal8Bit() + ":" + oldPath);
        {
            auto engine = createVibeEngine();
            Proposal proposal;
            QString error;
            QVERIFY2(runEngine(*engine, QStringLiteral("køb mælk"), &proposal, &error), qPrintable(error));
            QCOMPARE(proposal.tasks.size(), 1);
            QCOMPARE(proposal.tasks.first().title, QStringLiteral("Køb mælk"));
            QCOMPARE(proposal.tasks.first().start.date(), QDate(2026, 11, 24));
        }
        qputenv("PATH", oldPath);

        QFile marker(dir.filePath(QStringLiteral("marker")));
        QVERIFY(marker.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(marker.readAll()).trimmed(), QStringLiteral("existed"));
        QFile sp(dir.filePath(QStringLiteral("system-prompt.txt")));
        QVERIFY(sp.open(QIODevice::ReadOnly));
        const QString spText = QString::fromUtf8(sp.readAll());
        QVERIFY(spText.contains(QStringLiteral("2026-11-23"))); // fixture date in the prompt
        QVERIFY(spText.contains(QStringLiteral("opgaver")));    // schema embedded

        // Tools must be disabled (a glob matching no real tool) and one turn.
        QFile argsFile(dir.filePath(QStringLiteral("args.txt")));
        QVERIFY(argsFile.open(QIODevice::ReadOnly));
        const QString argsText = QString::fromUtf8(argsFile.readAll());
        QVERIFY(argsText.contains(QStringLiteral("--enabled-tools")));
        QVERIFY(argsText.contains(QStringLiteral("__omaplan_no_tools__")));
        QVERIFY(argsText.contains(QStringLiteral("--max-turns")));
    }

    void claudeEngineUnwrapsResultEnvelope()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // claude --output-format json answers {"result": "<schema json>"}.
        const QJsonObject envelope{{QStringLiteral("result"),
                                     QString::fromUtf8(proposalJson(QStringLiteral("Skriv blogindlæg")))}};
        QFile resp(dir.filePath(QStringLiteral("response.json")));
        QVERIFY(resp.open(QIODevice::WriteOnly));
        resp.write(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
        resp.close();

        const QString script = QStringLiteral(
            "#!/bin/sh\n"
            "dir=\"%1\"\n"
            "printf '%s\\n' \"$@\" > \"$dir/args.txt\"\n"
            "cat \"$dir/response.json\"\n").arg(dir.path());
        QVERIFY(writeFakeTool(dir.path(), QStringLiteral("claude"), script));

        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", dir.path().toLocal8Bit() + ":" + oldPath);
        {
            auto engine = createClaudeEngine();
            Proposal proposal;
            QString error;
            QVERIFY2(runEngine(*engine, QStringLiteral("skriv blogindlæg"), &proposal, &error), qPrintable(error));
            QCOMPARE(proposal.tasks.size(), 1);
            QCOMPARE(proposal.tasks.first().title, QStringLiteral("Skriv blogindlæg"));
        }
        qputenv("PATH", oldPath);

        // The full prompt went to -p, with the json output format requested.
        // (The prompt itself contains newlines, so inspect the raw file.)
        QFile args(dir.filePath(QStringLiteral("args.txt")));
        QVERIFY(args.open(QIODevice::ReadOnly));
        const QString argsText = QString::fromUtf8(args.readAll());
        const QStringList argLines = argsText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(argLines.contains(QStringLiteral("-p")));
        QVERIFY(argLines.contains(QStringLiteral("--output-format")));
        QVERIFY(argLines.contains(QStringLiteral("json")));
        const int p = argLines.indexOf(QStringLiteral("-p"));
        QVERIFY(p >= 0 && p + 1 < argLines.size());
        QVERIFY(argsText.contains(QStringLiteral("skriv blogindlæg")));   // user text
        QVERIFY(argsText.contains(QStringLiteral("2026-11-23")));         // system prompt included
    }

    void localEngineOllamaProtocol()
    {
        FakeHttpServer srv;
        const QJsonObject msg{{QStringLiteral("role"), QStringLiteral("assistant")},
                              {QStringLiteral("content"), QString::fromUtf8(proposalJson(QStringLiteral("Køb mælk")))}};
        srv.responseBody = QJsonDocument(QJsonObject{{QStringLiteral("message"), msg},
                                                      {QStringLiteral("done_reason"), QStringLiteral("stop")}})
                               .toJson(QJsonDocument::Compact);
        QVERIFY(srv.listen());
        auto engine = createLocalEngine(srv.endpoint(), QStringLiteral("test-model"), QStringLiteral("ollama"));
        QCOMPARE(engine->id(), QStringLiteral("lokal"));
        QCOMPARE(engine->modelInfo(), QStringLiteral("test-model"));
        QString why;
        QVERIFY(engine->available(&why)); // no external binary needed

        Proposal proposal;
        QString error;
        QVERIFY2(runEngine(*engine, QStringLiteral("køb mælk"), &proposal, &error), qPrintable(error));
        QCOMPARE(proposal.tasks.size(), 1);
        QCOMPARE(proposal.tasks.first().title, QStringLiteral("Køb mælk"));

        QCOMPARE(srv.requests, 1);
        QVERIFY2(srv.requestLine.startsWith("POST /api/chat"), qPrintable(srv.requestLine));
        const QJsonObject body = QJsonDocument::fromJson(srv.requestBody).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("test-model"));
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), false);
        QCOMPARE(body.value(QStringLiteral("think")).toBool(), false);
        QCOMPARE(body.value(QStringLiteral("temperature")).toDouble(), 0.0);
        QVERIFY(body.value(QStringLiteral("format")).isObject()); // schema as grammar
        QVERIFY(!body.contains(QStringLiteral("response_format")));
        const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
        QCOMPARE(messages.at(1).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        QVERIFY(messages.at(1).toObject().value(QStringLiteral("content")).toString()
                .contains(QStringLiteral("køb mælk")));
    }

    void localEngineOpenaiProtocol()
    {
        FakeHttpServer srv;
        const QJsonObject choice{{QStringLiteral("message"),
                                  QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                              {QStringLiteral("content"),
                                               QString::fromUtf8(proposalJson(QStringLiteral("Køb mælk")))}}},
                                 {QStringLiteral("finish_reason"), QStringLiteral("stop")}};
        srv.responseBody = QJsonDocument(QJsonObject{{QStringLiteral("choices"), QJsonArray{choice}}})
                               .toJson(QJsonDocument::Compact);
        QVERIFY(srv.listen());
        auto engine = createLocalEngine(srv.endpoint(), QStringLiteral("test-model"), QStringLiteral("openai"));
        Proposal proposal;
        QString error;
        QVERIFY2(runEngine(*engine, QStringLiteral("køb mælk"), &proposal, &error), qPrintable(error));
        QCOMPARE(proposal.tasks.size(), 1);

        QCOMPARE(srv.requests, 1);
        QVERIFY2(srv.requestLine.startsWith("POST /v1/chat/completions"), qPrintable(srv.requestLine));
        const QJsonObject body = QJsonDocument::fromJson(srv.requestBody).object();
        const QJsonObject responseFormat = body.value(QStringLiteral("response_format")).toObject();
        QCOMPARE(responseFormat.value(QStringLiteral("type")).toString(), QStringLiteral("json_schema"));
        QVERIFY(responseFormat.value(QStringLiteral("json_schema")).toObject()
                        .value(QStringLiteral("schema")).isObject());
        QVERIFY(!body.contains(QStringLiteral("format")));
        QVERIFY(!body.contains(QStringLiteral("think")));
    }

    void localEngineTruncationFailsWithRaw()
    {
        // Ollama: done_reason "length".
        {
            FakeHttpServer srv;
            const QString partial = QStringLiteral("OPGAVE-TRUNKERET-1");
            const QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
                                      {QStringLiteral("content"), partial}};
            const QJsonObject response{{QStringLiteral("message"), message},
                                       {QStringLiteral("done_reason"), QStringLiteral("length")}};
            srv.responseBody = QJsonDocument(response).toJson(QJsonDocument::Compact);
            QVERIFY(srv.listen());
            auto engine = createLocalEngine(srv.endpoint(), QStringLiteral("m"), QStringLiteral("ollama"));
            QString error;
            QVERIFY(!runEngine(*engine, QStringLiteral("x"), nullptr, &error));
            QVERIFY2(error.contains(QStringLiteral("truncated"), Qt::CaseInsensitive), qPrintable(error));
            QVERIFY2(error.contains(QStringLiteral("done_reason")), qPrintable(error)); // raw included
        }
        // OpenAI: finish_reason "length" inside choices[0].
        {
            FakeHttpServer srv;
            const QString partial = QStringLiteral("OPGAVE-TRUNKERET-2");
            const QJsonObject message{{QStringLiteral("content"), partial}};
            const QJsonObject choice{{QStringLiteral("message"), message},
                                     {QStringLiteral("finish_reason"), QStringLiteral("length")}};
            const QJsonObject response{{QStringLiteral("choices"), QJsonArray{choice}}};
            srv.responseBody = QJsonDocument(response).toJson(QJsonDocument::Compact);
            QVERIFY(srv.listen());
            auto engine = createLocalEngine(srv.endpoint(), QStringLiteral("m"), QStringLiteral("openai"));
            QString error;
            QVERIFY(!runEngine(*engine, QStringLiteral("x"), nullptr, &error));
            QVERIFY2(error.contains(QStringLiteral("truncated"), Qt::CaseInsensitive), qPrintable(error));
            QVERIFY2(error.contains(QStringLiteral("finish_reason")), qPrintable(error));
        }
    }

    void localEngineErrorsAreClear()
    {
        // Missing model/endpoint: error shows an example config, nothing hardcoded.
        {
            auto engine = createLocalEngine(QString(), QString(), QString());
            QString error;
            QVERIFY(!runEngine(*engine, QStringLiteral("x"), nullptr, &error));
            QVERIFY2(error.contains(QStringLiteral("Example config")), qPrintable(error));
            QVERIFY2(error.contains(QStringLiteral("config.toml")), qPrintable(error));
            QVERIFY2(error.contains(QStringLiteral("--endpoint")), qPrintable(error));
        }
        // Invalid endpoint.
        {
            auto engine = createLocalEngine(QStringLiteral("ikke en url"), QStringLiteral("m"), QString());
            QString error;
            QVERIFY(!runEngine(*engine, QStringLiteral("x"), nullptr, &error));
            QVERIFY2(error.contains(QStringLiteral("Invalid endpoint")), qPrintable(error));
        }
        // Server error envelope (Ollama style).
        {
            FakeHttpServer srv;
            srv.responseBody = R"({"error": "model 'mit-valg' not found"})";
            QVERIFY(srv.listen());
            auto engine = createLocalEngine(srv.endpoint(), QStringLiteral("mit-valg"), QStringLiteral("ollama"));
            QString error;
            QVERIFY(!runEngine(*engine, QStringLiteral("x"), nullptr, &error));
            QVERIFY2(error.contains(QStringLiteral("not found")), qPrintable(error));
            QVERIFY2(error.contains(QStringLiteral("mit-valg")), qPrintable(error));
        }
        // HTTP 500 without an error field.
        {
            FakeHttpServer srv;
            srv.responseStatus = 500;
            srv.responseBody = R"({"detail": "kaput"})";
            QVERIFY(srv.listen());
            auto engine = createLocalEngine(srv.endpoint(), QStringLiteral("m"), QStringLiteral("ollama"));
            QString error;
            QVERIFY(!runEngine(*engine, QStringLiteral("x"), nullptr, &error));
            QVERIFY2(error.contains(QStringLiteral("HTTP 500")), qPrintable(error));
            QVERIFY2(error.contains(QStringLiteral("kaput")), qPrintable(error)); // raw included
        }
        // Nothing listening.
        {
            FakeHttpServer srv;
            QVERIFY(srv.listen());
            const QString endpoint = srv.endpoint();
            srv.server.close(); // port is now free
            auto engine = createLocalEngine(endpoint, QStringLiteral("m"), QStringLiteral("ollama"));
            QString error;
            QVERIFY(!runEngine(*engine, QStringLiteral("x"), nullptr, &error));
            QVERIFY2(error.contains(QStringLiteral("not reachable"), Qt::CaseInsensitive), qPrintable(error));
        }
    }

    void createEngineFactoryReadsConfigAndOverrides()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString cfg = dir.filePath(QStringLiteral("config.toml"));
        QFile f(cfg);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("[motor]\n"
                               "engine = \"command\"\n"
                               "command = \"cat %1\"\n"
                               "\n"
                               "[motor.lokal]\n"
                               "api = \"openai\"\n"
                               "endpoint = \"http://127.0.0.1:9999\"\n"
                               "model = \"mit-valg\"\n")
                    .arg(fixturesDir() + QStringLiteral("/envelopes/command-valid.json"))
                    .toUtf8());
        f.close();
        qputenv("OMAPLAN_CONFIG", cfg.toLocal8Bit());
        {
            QString error;
            auto cmd = createEngine(QStringLiteral("command"), {}, {}, &error);
            QVERIFY2(cmd != nullptr, qPrintable(error));
            QCOMPARE(cmd->id(), QStringLiteral("command"));

            auto lokal = createEngine(QStringLiteral("lokal"), {}, {}, &error);
            QVERIFY2(lokal != nullptr, qPrintable(error));
            QCOMPARE(lokal->modelInfo(), QStringLiteral("mit-valg"));

            // CLI overrides win over config.toml.
            auto overridden = createEngine(QStringLiteral("lokal"), QStringLiteral("andet-valg"),
                                           QStringLiteral("http://127.0.0.1:1"), &error);
            QVERIFY(overridden != nullptr);
            QCOMPARE(overridden->modelInfo(), QStringLiteral("andet-valg"));

            // Unknown id: nullptr + clear error.
            QVERIFY(createEngine(QStringLiteral("nej"), {}, {}, &error) == nullptr);
            QVERIFY2(error.contains(QStringLiteral("Unknown engine")), qPrintable(error));
        }
        qputenv("OMAPLAN_CONFIG", "");
        // Without config, "command" needs a configured command.
        QString error;
        QVERIFY(createEngine(QStringLiteral("command"), {}, {}, &error) == nullptr);
        QVERIFY2(error.contains(QStringLiteral("command")), qPrintable(error));
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
    // The fake HTTP servers are loopback; never route them through a proxy.
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
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

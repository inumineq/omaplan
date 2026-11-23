#include "engine.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

// Local Ollama engine. Talks HTTP to 127.0.0.1:11434 (loopback only, no
// external service) and passes the schema as the "format" parameter so the
// model is steered towards valid JSON. The answer arrives in an envelope
// ({"response": "...", "done": true}) which parseProposal unwraps.
class OllamaEngine : public AiEngine {
public:
    explicit OllamaEngine(QObject *parent = nullptr)
        : AiEngine(parent)
        , m_nam(new QNetworkAccessManager(this))
    {
    }

    QString id() const override { return QStringLiteral("ollama"); }
    QString displayName() const override { return QStringLiteral("Ollama (%1)").arg(m_model); }

    // Ollama needs no external binary; reachability is reported per request.
    bool available(QString *whyNot) const override
    {
        Q_UNUSED(whyNot);
        return true;
    }

    void setModel(const QString &model) { m_model = model; }
    QString model() const { return m_model; }

    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }
    QUrl baseUrl() const { return m_baseUrl; }

protected:
    void start(const QString &systemPrompt, const QString &userPrompt) override
    {
        QJsonObject body;
        body.insert(QStringLiteral("model"), m_model);
        body.insert(QStringLiteral("system"), systemPrompt);
        body.insert(QStringLiteral("prompt"), userPrompt);
        body.insert(QStringLiteral("format"), schemaObject()); // steer output to the schema
        body.insert(QStringLiteral("stream"), false);

        QNetworkRequest request(m_baseUrl.resolved(QUrl(QStringLiteral("/api/generate"))));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        m_reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(m_reply, &QNetworkReply::readyRead, this, [this] {
            if (m_reply)
                m_replyBuffer += m_reply->readAll();
        });
        connect(m_reply, &QNetworkReply::finished, this, [this] {
            if (!isRunning() || !m_reply)
                return;
            QNetworkReply *reply = m_reply;
            m_reply = nullptr;
            m_replyBuffer += reply->readAll();
            const QNetworkReply::NetworkError netError = reply->error();
            const QString errorString = reply->errorString();
            const QByteArray body = m_replyBuffer;
            m_replyBuffer.clear();
            reply->deleteLater();
            if (netError != QNetworkReply::NoError) {
                fail(QStringLiteral("Ollama at %1 is not reachable (%2). Start Ollama or pick another engine.")
                         .arg(m_baseUrl.toString(), errorString));
                return;
            }
            // Ollama sometimes answers 200 with {"error": "..."} (e.g. an
            // unknown model); surface that as a clear failure.
            const QJsonObject obj = QJsonDocument::fromJson(body).object();
            if (obj.value(QStringLiteral("error")).isString()) {
                fail(QStringLiteral("Ollama error: %1").arg(obj.value(QStringLiteral("error")).toString()));
                return;
            }
            // finishWithRaw parses the envelope's "response" field.
            finishWithRaw(QString::fromUtf8(body));
        });
    }

    void kill() override
    {
        AiEngine::kill();
        if (m_reply) {
            m_reply->disconnect(this);
            m_reply->abort();
            m_reply->deleteLater();
            m_reply = nullptr;
        }
        m_replyBuffer.clear();
    }

private:
    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_replyBuffer;
    QString m_model = QStringLiteral("llama3.2");
    QUrl m_baseUrl = QUrl(QStringLiteral("http://127.0.0.1:11434"));
};

std::unique_ptr<AiEngine> createOllamaEngine(const QString &model, QObject *parent)
{
    auto engine = std::make_unique<OllamaEngine>(parent);
    if (!model.isEmpty())
        engine->setModel(model);
    return engine;
}

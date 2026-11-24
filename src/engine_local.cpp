#include "engine.h"
#include "config.h"
#include "omarchy.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

// One generic local engine (id "local") that speaks both Ollama's
// /api/chat and an OpenAI-compatible /v1/chat/completions (llama-server,
// LM Studio and others). Endpoint, model and protocol are the user's choice
// (config.toml [engine.local] or CLI flags --endpoint/--model); nothing is
// hardcoded and no model is bundled. The schema is sent as grammar (Ollama
// "format", OpenAI response_format json_schema) so small models stay valid.
// A truncated answer (done_reason/finish_reason "length") is an error and
// carries the raw response. Any URL is accepted, and the prompt (the text and
// the open tasks) goes to it, so the endpoint is meant to be the user's own
// machine or network. No API key is sent, except with api "omarchy": the
// model Omarchy itself runs (omarchy-local-ai), OpenAI-compatible on a
// loopback port, found through Omarchy's status file at each request, with
// Omarchy's key read from its file for that request and never stored.
class LocalEngine : public AiEngine {
public:
    LocalEngine(const QString &endpoint, const QString &model, const QString &api, QObject *parent)
        : AiEngine(parent)
        , m_endpoint(endpoint)
        , m_model(model)
        , m_omarchy(api == QLatin1String("omarchy") && endpoint.isEmpty())
        , m_api(api == QLatin1String("openai") || api == QLatin1String("omarchy") ? QLatin1String("openai")
                                                                                   : QLatin1String("ollama"))
        , m_nam(new QNetworkAccessManager(this))
    {
    }

    QString id() const override { return QStringLiteral("local"); }
    QString displayName() const override
    {
        const QString model = modelInfo();
        return model.isEmpty() ? AiEngine::tr("Local") : AiEngine::tr("Local (%1)").arg(model);
    }
    // Omarchy's model is named before the first request too.
    QString modelInfo() const override { return m_omarchy ? Omarchy::localModel(m_wanted).recipe : m_model; }

    // No external binary; reachability and configuration are reported per
    // request, so available() is true and start() fails clearly instead.
    bool available(QString *whyNot) const override
    {
        Q_UNUSED(whyNot);
        return true;
    }

protected:
    void start(const QString &systemPrompt, const QString &userPrompt) override
    {
        QByteArray key;
        if (m_omarchy) {
            // Asked now, so a model started or stopped since counts.
            const Omarchy::LocalModel model = Omarchy::localModel(m_wanted);
            key = Omarchy::localKey();
            if (!model.problem.isEmpty() || key.isEmpty()) {
                fail(!model.problem.isEmpty() ? model.problem
                                              : AiEngine::tr("Omarchy's local model key could not be read "
                                                             "(~/.config/omarchy/agent-local/key)."));
                return;
            }
            m_endpoint = model.endpoint;
            m_model = model.recipe;
        }
        if (m_model.isEmpty() || m_endpoint.isEmpty()) {
            fail(AiEngine::tr("The local engine needs a model and an endpoint. Example config "
                                "(~/.config/omaplan/config.toml):\n\n"
                                "[engine.local]\n"
                                "api = \"ollama\"\n"
                                "endpoint = \"http://127.0.0.1:11434\"\n"
                                "model = \"your-choice\"\n\n"
                                "or on the command line: omaplan add \"...\" --engine local "
                                "--endpoint http://127.0.0.1:11434 --model your-choice"));
            return;
        }

        const QUrl base(m_endpoint);
        if (!base.isValid() || base.host().isEmpty()) {
            fail(AiEngine::tr("Invalid endpoint '%1'. Example: http://127.0.0.1:11434").arg(m_endpoint));
            return;
        }

        QJsonArray messages;
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                    {QStringLiteral("content"), systemPrompt}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"), userPrompt}});

        QJsonObject body;
        body.insert(QStringLiteral("model"), m_model);
        body.insert(QStringLiteral("messages"), messages);
        body.insert(QStringLiteral("stream"), false);
        // Temperature 0 and (Ollama) think:false keep small models
        // deterministic and fast.
        body.insert(QStringLiteral("temperature"), 0.0);
        if (m_api == QLatin1String("ollama")) {
            body.insert(QStringLiteral("think"), false);
            body.insert(QStringLiteral("format"), schemaObject()); // schema as grammar
        } else {
            body.insert(QStringLiteral("response_format"),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("json_schema")},
                                    {QStringLiteral("json_schema"),
                                     QJsonObject{{QStringLiteral("name"), QStringLiteral("omaplan-proposal")},
                                                 {QStringLiteral("strict"), true},
                                                 {QStringLiteral("schema"), schemaObject()}}}});
        }

        const QString path = m_api == QLatin1String("ollama") ? QStringLiteral("/api/chat")
                                                               : QStringLiteral("/v1/chat/completions");
        QNetworkRequest request(base.resolved(QUrl(path)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (!key.isEmpty())
            request.setRawHeader("Authorization", "Bearer " + key);
        // Never followed: a redirect would carry the prompt, and the key, to
        // an address nobody chose. A 3xx answer is reported as an error.
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

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
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray bodyBytes = m_replyBuffer;
            m_replyBuffer.clear();
            reply->deleteLater();

            // HTTP status errors (QNetworkAccessManager also reports them as
            // network errors) carry the body -- show status + raw response.
            if (httpStatus >= 300 && httpStatus < 400) {
                fail(AiEngine::tr("Local engine at %1 answered HTTP %2, a redirect to another address. "
                                  "Omaplan does not follow it; give the server's own address.")
                         .arg(m_endpoint).arg(httpStatus));
                return;
            }
            if (httpStatus >= 400) {
                fail(AiEngine::tr("Local engine at %1 answered HTTP %2.\n\nRaw response:\n%3")
                         .arg(m_endpoint).arg(httpStatus).arg(QString::fromUtf8(bodyBytes)));
                return;
            }
            if (netError != QNetworkReply::NoError) {
                fail(AiEngine::tr("Local engine at %1 is not reachable (%2, HTTP %3). Is the server running?")
                         .arg(m_endpoint, errorString).arg(httpStatus));
                return;
            }
            const QJsonObject obj = QJsonDocument::fromJson(bodyBytes).object();
            if (obj.value(QStringLiteral("error")).isString()) {
                fail(AiEngine::tr("Local engine error: %1\n\nRaw response:\n%2")
                         .arg(obj.value(QStringLiteral("error")).toString(), QString::fromUtf8(bodyBytes)));
                return;
            }
            // Truncated answers are errors, not partial plans. OpenAI puts
            // finish_reason inside choices[0], Ollama at the top level.
            const QString doneReason = obj.value(QStringLiteral("done_reason")).toString();
            QString finishReason = obj.value(QStringLiteral("finish_reason")).toString();
            const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
            if (finishReason.isEmpty() && !choices.isEmpty())
                finishReason = choices.first().toObject().value(QStringLiteral("finish_reason")).toString();
            const QString truncation = doneReason == QLatin1String("length") ? doneReason
                                       : finishReason == QLatin1String("length") ? finishReason
                                                                                   : QString();
            if (!truncation.isEmpty()) {
                fail(AiEngine::tr("The model's answer was truncated (%1) before it finished. "
                                    "Raw response:\n%2").arg(truncation, QString::fromUtf8(bodyBytes)));
                return;
            }
            // finishWithRaw unwraps the envelope (message.content / choices[0])
            // and validates against the schema.
            finishWithRaw(QString::fromUtf8(bodyBytes));
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
    QString m_endpoint;
    QString m_model;
    // With Omarchy's model: the recipe the config asks for (empty = Omarchy's
    // pick), while m_model and m_endpoint are what the last request found.
    bool m_omarchy;
    QString m_wanted = m_omarchy ? m_model : QString();
    QString m_api;
    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_replyBuffer;
};

std::unique_ptr<AiEngine> createLocalEngine(const QString &endpoint, const QString &model,
                                             const QString &api, QObject *parent)
{
    return std::make_unique<LocalEngine>(endpoint, model, api, parent);
}

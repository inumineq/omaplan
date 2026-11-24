#include "omarchy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace Omarchy {

namespace {

QString firstLine(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readLine()).trimmed();
}

QString stateDirectory() {
    // XDG_STATE_HOME is the engine's own rule; a test home has no such override.
    const QString xdg = qEnvironmentVariable("XDG_STATE_HOME");
    const bool overridden = !qEnvironmentVariableIsEmpty("OMAPLAN_OMARCHY_HOME");
    return (xdg.isEmpty() || overridden ? home() + QStringLiteral("/.local/state") : xdg)
           + QStringLiteral("/omarchy/agent-local");
}

QString keyFile() { return home() + QStringLiteral("/.config/omarchy/agent-local/key"); }

bool isLoopback(const QUrl &url) {
    const QString host = url.host();
    return host == QLatin1String("localhost") || QHostAddress(host).isLoopback();
}

} // namespace

QString home() {
    const QString overridden = qEnvironmentVariable("OMAPLAN_OMARCHY_HOME");
    return overridden.isEmpty() ? QDir::homePath() : overridden;
}

QString defaultAgent() { return firstLine(home() + QStringLiteral("/.config/omarchy/defaults/agent")); }

QString defaultEngine() {
    const QString agent = defaultAgent();
    if (agent == QLatin1String("claude") || agent == QLatin1String("local"))
        return agent;
    return {};
}

bool hasLocalModels() { return QFile::exists(keyFile()); }

LocalModel localModel(const QString &wanted) {
    LocalModel model;
    QFile file(stateDirectory() + QStringLiteral("/status.json"));
    const QJsonObject status = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()
                                                              : QJsonObject();
    const QJsonObject models = status.value(QLatin1String("models")).toObject();
    const auto isText = [&models](const QString &recipe) {
        const QJsonObject m = models.value(recipe).toObject();
        return !m.isEmpty() && m.value(QLatin1String("kind")).toString(QStringLiteral("text")) == QLatin1String("text");
    };

    // A pinned host/recipe is a paired machine's model, which is not in this
    // machine's status and has a key of its own; it is passed over.
    QString pinned = firstLine(home() + QStringLiteral("/.config/omarchy/defaults/local-model"));
    if (pinned.contains(QLatin1Char('/')))
        pinned.clear();
    QStringList candidates{wanted, pinned, status.value(QLatin1String("last")).toString(),
                           status.value(QLatin1String("best")).toString()};
    for (auto it = models.begin(); it != models.end(); ++it)
        if (it.value().toObject().value(QLatin1String("unit")).toString() == QLatin1String("active"))
            candidates << it.key();
    candidates.removeAll(QString());

    if (!wanted.isEmpty() && !isText(wanted)) {
        model.problem = QCoreApplication::translate("Omarchy", "Omarchy has no local text model named %1. See the ones set up with: omarchy models status")
                            .arg(wanted);
        return model;
    }
    for (const QString &candidate : std::as_const(candidates)) {
        if (isText(candidate)) {
            model.recipe = candidate;
            break;
        }
    }
    if (model.recipe.isEmpty()) {
        model.problem = QCoreApplication::translate("Omarchy", "Omarchy has no local text model set up. Set one up with: omarchy models use <recipe>");
        return model;
    }

    const QJsonObject chosen = models.value(model.recipe).toObject();
    if (chosen.value(QLatin1String("unit")).toString() != QLatin1String("active")) {
        model.problem = QCoreApplication::translate("Omarchy", "Omarchy's local model %1 is not running. Start it with: omarchy models start %1")
                            .arg(model.recipe);
        return model;
    }
    model.endpoint = chosen.value(QLatin1String("endpoint")).toString();
    if (model.endpoint.isEmpty() && chosen.value(QLatin1String("port")).toInt() > 0)
        model.endpoint = QStringLiteral("http://127.0.0.1:%1/v1").arg(chosen.value(QLatin1String("port")).toInt());
    // The key only ever goes to this machine.
    const QUrl url(model.endpoint);
    if (!url.isValid() || !isLoopback(url))
        model.problem = QCoreApplication::translate("Omarchy", "Omarchy's local model %1 has no endpoint on this machine (%2).")
                            .arg(model.recipe, model.endpoint.isEmpty() ? QStringLiteral("-") : model.endpoint);
    return model;
}

QByteArray localKey() {
    QFile file(keyFile());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll().trimmed();
}

} // namespace Omarchy

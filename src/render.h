#pragma once

#include <QDate>
#include <QJsonObject>
#include <QString>

// Server-side rendering of the read-only edition: plan.json (docs/export.md)
// to exactly the markup web/MARKUP.md describes. A line-by-line port of
// renderPage() in web/app.js, so web/index.html and web/index.da.html are
// byte-identical to page(web/example/plan.json, 2026-11-23, "en"/"da") --
// a test holds them together. Page texts come from web/i18n/<language>.json,
// embedded through src/web.qrc.
namespace Render {

// The four view ids, in page order.
inline const char *const kViews[] = {"overview", "list", "list-project", "timeline"};
bool isView(const QString &id);

// The page languages; "en" is the source and the fallback.
inline const char *const kLanguages[] = {"en", "da"};
// The supported language for a BCP 47 tag: da* -> "da", en* -> "en",
// anything else empty.
QString supportedLanguage(const QString &tag);

// `language` picks the dictionary (unsupported = "en"). `htmlStyle` goes
// inline on <html> (CSS variables of the theme, empty = none). `activeView`
// (one of kViews, empty = none) gets the class "active", so a query link
// (?view=list) selects a view without JavaScript.
QString page(const QJsonObject &plan, const QDate &today, const QString &language = QStringLiteral("en"),
             const QString &htmlStyle = {}, const QString &activeView = {});

// A friendly page in the same frame, for when there is no plan to show.
enum class Message { NoPlan, Unreadable };
QString messagePage(Message message, const QString &language = QStringLiteral("en"), const QString &htmlStyle = {});

// HTML escaping as in app.js: & < > " '
QString esc(const QString &text);

} // namespace Render

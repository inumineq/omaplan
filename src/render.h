#pragma once

#include <QDate>
#include <QJsonObject>
#include <QString>

// Server-side rendering of the read-only edition: plan.json (docs/export.md)
// to exactly the markup web/MARKUP.md describes. A line-by-line port of
// renderSide() in web/app.js, so web/index.html is byte-identical to
// page(web/example/plan.json, 2026-11-23) -- a test holds the two together.
namespace Render {

// The four view ids, in page order.
inline const char *const kViews[] = {"overblik", "liste", "liste-projekt", "tidslinje"};
bool isView(const QString &id);

// `htmlStyle` goes inline on <html> (CSS variables of the theme, empty = none).
// `activeView` (one of kViews, empty = none) gets the class "aktiv", so a
// query link (?visning=liste) selects a view without JavaScript.
QString page(const QJsonObject &plan, const QDate &today, const QString &htmlStyle = {},
             const QString &activeView = {});

// A friendly page in the same frame, for when there is no plan to show yet.
QString messagePage(const QString &heading, const QString &text, const QString &htmlStyle = {});

// HTML escaping as in app.js: & < > " '
QString esc(const QString &text);

} // namespace Render

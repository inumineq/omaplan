#pragma once

#include <QAbstractListModel>
#include <QDate>
#include <QSortFilterProxyModel>

#include "store.h"
#include "task.h"

// The task list as a QAbstractListModel over the store; QML never touches the
// database directly. reload() re-reads everything. startLivePolling() polls
// PRAGMA data_version (default every 500 ms), so changes committed by other
// processes (CLI, agents) reload the model and emit externalChange() with the
// source from the newest `changes` entry ("cli", "mcp", ...).
class PlanModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        NotesRole,
        ProjectRole,
        PriorityRole,
        StatusRole,
        StartRole,          // QDateTime
        StartHasTimeRole,
        StartDateRole,      // QDate, for section headers
        DurationRole,       // minutes
        DeadlineRole,       // QDateTime
        DeadlineHasTimeRole,
        DeadlineDateRole,   // QDate
        GuessesRole,        // QStringList
        CreatedRole,
        ModifiedRole,
    };

    explicit PlanModel(Store *store, QObject *parent = nullptr);

    Q_INVOKABLE void reload();
    // Starts polling the database for changes from other processes.
    Q_INVOKABLE void startLivePolling(int intervalMs = 500);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();
    // Emitted when another process committed: source is from `changes`
    // ("cli", "mcp", "gui", ...), empty when unknown.
    void externalChange(const QString &source);

private slots:
    void pollExternalChanges();

private:
    Store *m_store;
    QList<Task> m_tasks;
    class QTimer *m_liveTimer = nullptr;
    int m_lastDataVersion = -1;
};

// Base for the view proxies: dynamic sort/filter, and sort once the source is
// set (QSortFilterProxyModel does not sort until told to).
class SortProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit SortProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {
        setDynamicSortFilter(true);
    }
    void setSourceModel(QAbstractItemModel *model) override {
        QSortFilterProxyModel::setSourceModel(model);
        sort(0, Qt::AscendingOrder);
    }
};

// Overblik: overdue tasks first, then everything due within the next 7 days.
class OverviewModel : public SortProxy {
    Q_OBJECT
public:
    explicit OverviewModel(QObject *parent = nullptr);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};

// Kalender: only tasks whose effective date is inside [from, to].
class CalendarModel : public SortProxy {
    Q_OBJECT
    Q_PROPERTY(QDate from READ from WRITE setFrom NOTIFY rangeChanged)
    Q_PROPERTY(QDate to READ to WRITE setTo NOTIFY rangeChanged)

public:
    explicit CalendarModel(QObject *parent = nullptr);

    QDate from() const { return m_from; }
    QDate to() const { return m_to; }
    void setFrom(const QDate &from);
    void setTo(const QDate &to);
    Q_INVOKABLE void setRange(const QDate &from, const QDate &to);

signals:
    void rangeChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QDate m_from, m_to;
};

// Liste: all tasks (optionally filtered to one project); undone first, then
// by effective date.
class PlanListModel : public SortProxy {
    Q_OBJECT
    Q_PROPERTY(QString projectFilter READ projectFilter WRITE setProjectFilter NOTIFY projectFilterChanged)

public:
    explicit PlanListModel(QObject *parent = nullptr);

    QString projectFilter() const { return m_projectFilter; }
    void setProjectFilter(const QString &filter);

signals:
    void projectFilterChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_projectFilter;
};

// Tidslinje: only scheduled tasks (a bar needs a start), sorted by start.
class GanttModel : public SortProxy {
    Q_OBJECT
public:
    explicit GanttModel(QObject *parent = nullptr);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};

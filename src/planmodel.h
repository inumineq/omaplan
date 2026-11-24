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
    // Bumped on every reload, so bindings over rows() re-evaluate.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    // Today's date; changes at midnight while the window is open.
    Q_PROPERTY(QDate today READ today NOTIFY todayChanged)
    // Why the plan could not be read (database missing, locked, newer
    // schema); empty when all is well. Shown in the window.
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

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
        DayKeyRole,         // ISO day the task belongs to (start, else deadline); "" when undated
        SpanRole,           // its extent on the timeline (taskSpan) as a map
    };

    explicit PlanModel(Store *store, QObject *parent = nullptr);

    Q_INVOKABLE void reload();
    // Starts polling the database for changes from other processes.
    Q_INVOKABLE void startLivePolling(int intervalMs = 500);
    QDate today() const { return m_today; }
    int revision() const { return m_revision; }
    QString error() const { return m_error; }
    // Error from opening the store, so the window can say what is wrong.
    void setStoreError(const QString &error);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();
    void todayChanged();
    void revisionChanged();
    void errorChanged();
    // Emitted when another process committed: source is from `changes`
    // ("cli", "mcp", "gui", ...), empty when unknown.
    void externalChange(const QString &source);

private slots:
    void pollExternalChanges();
    void checkDay();

private:
    Store *m_store;
    QList<Task> m_tasks;
    class QTimer *m_liveTimer = nullptr;
    int m_lastDataVersion = -1;
    class QTimer *m_dayTimer = nullptr;
    QDate m_today;
    QString m_error;
    QString m_storeError;
    int m_revision = 0;
};

// Base for the view proxies: dynamic sort/filter, and sort once the source is
// set (QSortFilterProxyModel does not sort until told to).
class SortProxy : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY contentChanged)
    // Bumped whenever the visible rows change (filter, sort, data), so QML
    // bindings over rows() and ids() follow this model and nothing else.
    Q_PROPERTY(int revision READ revision NOTIFY contentChanged)
public:
    explicit SortProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {
        setDynamicSortFilter(true);
        const auto bump = [this] { ++m_revision; emit contentChanged(); };
        connect(this, &QAbstractItemModel::modelReset, this, bump);
        connect(this, &QAbstractItemModel::layoutChanged, this, bump);
        connect(this, &QAbstractItemModel::rowsInserted, this, bump);
        connect(this, &QAbstractItemModel::rowsRemoved, this, bump);
        connect(this, &QAbstractItemModel::rowsMoved, this, bump);
        connect(this, &QAbstractItemModel::dataChanged, this, bump);
    }
    int revision() const { return m_revision; }
    void setSourceModel(QAbstractItemModel *model) override {
        QSortFilterProxyModel::setSourceModel(model);
        sort(0, Qt::AscendingOrder);
    }
    // Task ids in display order, for keyboard navigation in the views.
    Q_INVOKABLE QStringList ids() const;
    // All rows as {roleName: value} maps, for views that lay tasks out
    // themselves (time grid, month cells, timeline lanes).
    Q_INVOKABLE QVariantList rows() const;
signals:
    void contentChanged();
private:
    int m_revision = 0;
};

// Registers the view models and the Fmt singleton under "import Omaplan".
void registerQmlTypes();

// Overblik: overdue tasks first, then everything due within the next 7 days,
// then open tasks without a date. The extra role "agendaSection" is the
// header each row sits under: "overdue", an ISO day, or "none". A task
// already started but due later sits under today, since it is being worked on.
class OverviewModel : public SortProxy {
    Q_OBJECT
public:
    enum { AgendaSectionRole = PlanModel::DayKeyRole + 100 };
    explicit OverviewModel(QObject *parent = nullptr);
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    // The day a task is listed under; invalid for overdue/unscheduled.
    static QDate agendaDate(const Task &task, const QDate &today);

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
    // "" = all, "todo" or "done".
    Q_PROPERTY(QString statusFilter READ statusFilter WRITE setStatusFilter NOTIFY statusFilterChanged)
    // "" = by date, "project" = by project then date (so sections group).
    Q_PROPERTY(QString grouping READ grouping WRITE setGrouping NOTIFY groupingChanged)

public:
    explicit PlanListModel(QObject *parent = nullptr);

    QString projectFilter() const { return m_projectFilter; }
    void setProjectFilter(const QString &filter);
    QString statusFilter() const { return m_statusFilter; }
    void setStatusFilter(const QString &filter);
    QString grouping() const { return m_grouping; }
    void setGrouping(const QString &grouping);

signals:
    void projectFilterChanged();
    void statusFilterChanged();
    void groupingChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_projectFilter;
    QString m_statusFilter;
    QString m_grouping;
};

// Timeline: the tasks whose span (bar or deadline mark, see taskSpan) reaches
// into [from, to], also those that began before it; every task with a start
// or a deadline when no range is set. Sorted by where they begin.
class GanttModel : public SortProxy {
    Q_OBJECT
    Q_PROPERTY(QDate from READ from WRITE setFrom NOTIFY rangeChanged)
    Q_PROPERTY(QDate to READ to WRITE setTo NOTIFY rangeChanged)
public:
    explicit GanttModel(QObject *parent = nullptr);

    QDate from() const { return m_from; }
    QDate to() const { return m_to; }
    void setFrom(const QDate &from);
    void setTo(const QDate &to);

    // Timeline::layout for the view: items are maps {barStart, barEnd,
    // hasMark, markStart, markEnd, labelWidth}; the answer is one map per item
    // {lane, side ("inside", "afterBar", "afterMark", "before"), labelStart,
    // labelWidth, shortened, start, end (all the task occupies)}.
    Q_INVOKABLE QVariantList pack(const QVariantList &items, double total, double gap, double pad, double spacing) const;

signals:
    void rangeChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QDate m_from, m_to;
};

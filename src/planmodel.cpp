#include "planmodel.h"

#include <QQmlEngine>
#include <QTimer>
#include <QUrl>

PlanModel::PlanModel(Store *store, QObject *parent)
    : QAbstractListModel(parent), m_store(store), m_today(QDate::currentDate()) {
    // A window left open overnight must move "today" along: check once a
    // minute, and on a new day reload, so every proxy re-filters.
    m_dayTimer = new QTimer(this);
    m_dayTimer->setInterval(60 * 1000);
    connect(m_dayTimer, &QTimer::timeout, this, &PlanModel::checkDay);
    m_dayTimer->start();
}

void PlanModel::checkDay() {
    const QDate now = QDate::currentDate();
    if (now == m_today)
        return;
    m_today = now;
    emit todayChanged();
    reload();
}

void PlanModel::reload() {
    beginResetModel();
    QString error;
    m_tasks = m_store->isOpen() ? m_store->tasks(&error) : QList<Task>();
    endResetModel();
    ++m_revision;
    emit revisionChanged();
    emit countChanged();
    if (!m_store->isOpen() && error.isEmpty())
        error = m_storeError;
    if (error != m_error) {
        m_error = error;
        emit errorChanged();
    }
}

void PlanModel::setStoreError(const QString &error) {
    m_storeError = error;
    reload();
}

void PlanModel::startLivePolling(int intervalMs) {
    if (!m_liveTimer) {
        m_liveTimer = new QTimer(this);
        m_liveTimer->setInterval(intervalMs);
        connect(m_liveTimer, &QTimer::timeout, this, &PlanModel::pollExternalChanges);
    }
    bool ok = false;
    m_lastDataVersion = m_store->dataVersion(&ok);
    if (!ok)
        m_lastDataVersion = -1;
    m_liveTimer->start();
}

void PlanModel::pollExternalChanges() {
    bool ok = false;
    const int version = m_store->dataVersion(&ok);
    if (!ok || version == m_lastDataVersion)
        return; // nothing new from other processes (own writes come via planChanged)
    m_lastDataVersion = version;
    reload();
    // Name the source of the newest change ("fra CLI" in the GUI).
    QString error;
    Store::Change change;
    QString source;
    if (m_store->newestChange(&change, &error))
        source = change.source;
    emit externalChange(source);
}

int PlanModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_tasks.size();
}

QVariant PlanModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size())
        return {};
    const Task &task = m_tasks.at(index.row());
    switch (role) {
    case IdRole: return task.id;
    case TitleRole: return task.title;
    case NotesRole: return task.notes;
    case ProjectRole: return task.project;
    case PriorityRole: return task.priority;
    case StatusRole: return Task::statusToString(task.status);
    case StartRole: return task.start;
    case StartHasTimeRole: return task.startHasTime;
    case StartDateRole: return task.start.isValid() ? task.start.date() : QDate();
    case DurationRole: return task.durationMinutes;
    case DeadlineRole: return task.deadline;
    case DeadlineHasTimeRole: return task.deadlineHasTime;
    case DeadlineDateRole: return task.deadline.isValid() ? task.deadline.date() : QDate();
    case GuessesRole: return task.guesses;
    case CreatedRole: return task.created;
    case ModifiedRole: return task.modified;
    case DayKeyRole:
        return task.start.isValid() || task.deadline.isValid() ? effectiveDate(task).toString(Qt::ISODate) : QString();
    default: return {};
    }
}

QHash<int, QByteArray> PlanModel::roleNames() const {
    return {
        {IdRole, "taskId"},
        {TitleRole, "title"},
        {NotesRole, "notes"},
        {ProjectRole, "project"},
        {PriorityRole, "priority"},
        {StatusRole, "status"},
        {StartRole, "start"},
        {StartHasTimeRole, "startHasTime"},
        {StartDateRole, "startDate"},
        {DurationRole, "durationMinutes"},
        {DeadlineRole, "deadline"},
        {DeadlineHasTimeRole, "deadlineHasTime"},
        {DeadlineDateRole, "deadlineDate"},
        {GuessesRole, "guesses"},
        {CreatedRole, "created"},
        {ModifiedRole, "modified"},
        {DayKeyRole, "dayKey"},
    };
}

static Task taskOf(const QAbstractItemModel *model, const QModelIndex &index) {
    Task task;
    task.id = model->data(index, PlanModel::IdRole).toString();
    task.title = model->data(index, PlanModel::TitleRole).toString();
    task.notes = model->data(index, PlanModel::NotesRole).toString();
    task.project = model->data(index, PlanModel::ProjectRole).toString();
    task.priority = model->data(index, PlanModel::PriorityRole).toInt();
    bool statusOk = false;
    task.status = Task::statusFromString(model->data(index, PlanModel::StatusRole).toString(), &statusOk);
    task.start = model->data(index, PlanModel::StartRole).toDateTime();
    task.startHasTime = model->data(index, PlanModel::StartHasTimeRole).toBool();
    task.durationMinutes = model->data(index, PlanModel::DurationRole).toInt();
    task.deadline = model->data(index, PlanModel::DeadlineRole).toDateTime();
    task.deadlineHasTime = model->data(index, PlanModel::DeadlineHasTimeRole).toBool();
    task.guesses = model->data(index, PlanModel::GuessesRole).toStringList();
    task.created = model->data(index, PlanModel::CreatedRole).toDateTime();
    task.modified = model->data(index, PlanModel::ModifiedRole).toDateTime();
    return task;
}


OverviewModel::OverviewModel(QObject *parent) : SortProxy(parent) {
    setSortRole(PlanModel::StartRole);
}

QDate OverviewModel::agendaDate(const Task &task, const QDate &today) {
    if (isOverdue(task, today) || (!task.start.isValid() && !task.deadline.isValid()))
        return {};
    QDate date = effectiveDate(task);
    if (date < today && task.status == Task::Status::Todo)
        date = today; // started earlier, due later: in progress today
    return date;
}

static QString agendaSection(const Task &task, const QDate &today) {
    if (isOverdue(task, today))
        return QStringLiteral("overdue");
    const QDate date = OverviewModel::agendaDate(task, today);
    return date.isValid() ? date.toString(Qt::ISODate) : QStringLiteral("none");
}

QVariant OverviewModel::data(const QModelIndex &index, int role) const {
    if (role != AgendaSectionRole)
        return SortProxy::data(index, role);
    return agendaSection(taskOf(sourceModel(), mapToSource(index)), QDate::currentDate());
}

QHash<int, QByteArray> OverviewModel::roleNames() const {
    QHash<int, QByteArray> names = SortProxy::roleNames();
    names.insert(AgendaSectionRole, "agendaSection");
    return names;
}

bool OverviewModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    const Task task = taskOf(sourceModel(), sourceModel()->index(sourceRow, 0, sourceParent));
    const QDate today = QDate::currentDate();
    if (isOverdue(task, today))
        return true;
    // Open unscheduled tasks belong on the agenda too (last, "No date").
    if (!task.start.isValid() && !task.deadline.isValid())
        return task.status == Task::Status::Todo;
    const QDate date = agendaDate(task, today);
    return date >= today && date <= today.addDays(7);
}

bool OverviewModel::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    const Task a = taskOf(sourceModel(), left);
    const Task b = taskOf(sourceModel(), right);
    const QDate today = QDate::currentDate();
    const bool overdueA = isOverdue(a, today), overdueB = isOverdue(b, today);
    if (overdueA != overdueB)
        return overdueA;
    const QDate dateA = overdueA ? effectiveDate(a) : agendaDate(a, today);
    const QDate dateB = overdueB ? effectiveDate(b) : agendaDate(b, today);
    if (dateA.isValid() != dateB.isValid())
        return dateA.isValid();
    if (dateA != dateB)
        return dateA < dateB;
    // Timed tasks in time order before all-day tasks of the same day.
    if (a.startHasTime != b.startHasTime)
        return a.startHasTime;
    if (a.start != b.start)
        return a.start < b.start;
    return a.title < b.title;
}

CalendarModel::CalendarModel(QObject *parent) : SortProxy(parent) {
    setSortRole(PlanModel::StartRole);
}

void CalendarModel::setFrom(const QDate &from) {
    if (m_from == from)
        return;
    m_from = from;
    emit rangeChanged();
    invalidateFilter();
}

void CalendarModel::setTo(const QDate &to) {
    if (m_to == to)
        return;
    m_to = to;
    emit rangeChanged();
    invalidateFilter();
}

void CalendarModel::setRange(const QDate &from, const QDate &to) {
    m_from = from;
    m_to = to;
    emit rangeChanged();
    invalidateFilter();
}

bool CalendarModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    if (!m_from.isValid() || !m_to.isValid())
        return true;
    const Task task = taskOf(sourceModel(), sourceModel()->index(sourceRow, 0, sourceParent));
    // A calendar shows dated tasks only; undated ones live in the list.
    if (!task.start.isValid() && !task.deadline.isValid())
        return false;
    const QDate date = effectiveDate(task);
    return date >= m_from && date <= m_to;
}

bool CalendarModel::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    // Day by day; within a day all-day tasks (shown on top) before timed
    // ones in time order — the order the week view draws them in.
    const Task a = taskOf(sourceModel(), left);
    const Task b = taskOf(sourceModel(), right);
    const QDate dateA = effectiveDate(a), dateB = effectiveDate(b);
    if (dateA != dateB)
        return dateA < dateB;
    if (a.startHasTime != b.startHasTime)
        return !a.startHasTime;
    if (a.start != b.start)
        return a.start < b.start;
    return a.title.localeAwareCompare(b.title) < 0;
}

PlanListModel::PlanListModel(QObject *parent) : SortProxy(parent) {
}

void PlanListModel::setProjectFilter(const QString &filter) {
    if (m_projectFilter == filter)
        return;
    m_projectFilter = filter;
    emit projectFilterChanged();
    invalidateFilter();
}

void PlanListModel::setStatusFilter(const QString &filter) {
    if (m_statusFilter == filter)
        return;
    m_statusFilter = filter;
    emit statusFilterChanged();
    invalidateFilter();
}

void PlanListModel::setGrouping(const QString &grouping) {
    if (m_grouping == grouping)
        return;
    m_grouping = grouping;
    emit groupingChanged();
    invalidate();
}

bool PlanListModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    const Task task = taskOf(sourceModel(), sourceModel()->index(sourceRow, 0, sourceParent));
    if (!m_projectFilter.isEmpty() && task.project != m_projectFilter)
        return false;
    return m_statusFilter.isEmpty() || Task::statusToString(task.status) == m_statusFilter;
}

bool PlanListModel::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    const Task a = taskOf(sourceModel(), left);
    const Task b = taskOf(sourceModel(), right);
    // Grouped by project: projects alphabetically, "no project" last, so
    // each project's rows sit together under one section header.
    if (m_grouping == QLatin1String("project") && a.project != b.project) {
        if (a.project.isEmpty() != b.project.isEmpty())
            return !a.project.isEmpty();
        return a.project.localeAwareCompare(b.project) < 0;
    }
    // Dated tasks by day (timed before all-day), undated last; done ones
    // stay in place so a checked task does not jump away under the cursor.
    const bool datedA = a.start.isValid() || a.deadline.isValid();
    const bool datedB = b.start.isValid() || b.deadline.isValid();
    if (datedA != datedB)
        return datedA;
    const QDate dateA = effectiveDate(a), dateB = effectiveDate(b);
    if (dateA != dateB)
        return dateA < dateB;
    if (a.startHasTime != b.startHasTime)
        return a.startHasTime;
    if (a.start != b.start)
        return a.start < b.start;
    return a.title.localeAwareCompare(b.title) < 0;
}

GanttModel::GanttModel(QObject *parent) : SortProxy(parent) {
    setSortRole(PlanModel::StartRole);
}

bool GanttModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    const Task task = taskOf(sourceModel(), sourceModel()->index(sourceRow, 0, sourceParent));
    return task.start.isValid();
}

bool GanttModel::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    return taskOf(sourceModel(), left).start < taskOf(sourceModel(), right).start;
}

QStringList SortProxy::ids() const {
    QStringList result;
    for (int row = 0; row < rowCount(); ++row)
        result << data(index(row, 0), PlanModel::IdRole).toString();
    return result;
}

QVariantList SortProxy::rows() const {
    QVariantList result;
    const QHash<int, QByteArray> names = roleNames();
    for (int row = 0; row < rowCount(); ++row) {
        QVariantMap map;
        const QModelIndex idx = index(row, 0);
        for (auto it = names.begin(); it != names.end(); ++it)
            map.insert(QString::fromLatin1(it.value()), data(idx, it.key()));
        result << map;
    }
    return result;
}

void registerQmlTypes() {
    qmlRegisterType<OverviewModel>("Omaplan", 1, 0, "OverviewModel");
    qmlRegisterType<CalendarModel>("Omaplan", 1, 0, "CalendarModel");
    qmlRegisterType<PlanListModel>("Omaplan", 1, 0, "PlanListModel");
    qmlRegisterType<GanttModel>("Omaplan", 1, 0, "GanttModel");
    // Date, time and duration formatting in the Omarchy language.
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/Fmt.qml")), "Omaplan", 1, 0, "Fmt");
}

#include "planmodel.h"

PlanModel::PlanModel(Store *store, QObject *parent)
    : QAbstractListModel(parent), m_store(store) {
}

void PlanModel::reload() {
    beginResetModel();
    QString error;
    m_tasks = m_store->tasks(&error);
    endResetModel();
    emit countChanged();
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

static bool isOverdue(const Task &task, const QDate &today) {
    return task.status == Task::Status::Todo && task.deadline.isValid() && task.deadline.date() < today;
}

OverviewModel::OverviewModel(QObject *parent) : SortProxy(parent) {
    setSortRole(PlanModel::StartRole);
}

bool OverviewModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    const Task task = taskOf(sourceModel(), sourceModel()->index(sourceRow, 0, sourceParent));
    const QDate today = QDateTime::currentDateTime().date();
    return isOverdue(task, today) || effectiveDate(task) <= today.addDays(7);
}

bool OverviewModel::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    const Task a = taskOf(sourceModel(), left);
    const Task b = taskOf(sourceModel(), right);
    const QDate today = QDateTime::currentDateTime().date();
    const bool overdueA = isOverdue(a, today), overdueB = isOverdue(b, today);
    if (overdueA != overdueB)
        return overdueA;
    const QDate dateA = effectiveDate(a), dateB = effectiveDate(b);
    if (dateA != dateB)
        return dateA < dateB;
    return a.start < b.start;
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
    const QDate date = effectiveDate(task);
    return date >= m_from && date <= m_to;
}

bool CalendarModel::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    const Task a = taskOf(sourceModel(), left);
    const Task b = taskOf(sourceModel(), right);
    if (a.start.isValid() != b.start.isValid())
        return a.start.isValid();
    if (a.start != b.start)
        return a.start < b.start;
    return effectiveDate(a) < effectiveDate(b);
}

PlanListModel::PlanListModel(QObject *parent) : SortProxy(parent) {
}

bool PlanListModel::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    const Task a = taskOf(sourceModel(), left);
    const Task b = taskOf(sourceModel(), right);
    if (a.status != b.status)
        return a.status == Task::Status::Todo; // undone first
    const QDate dateA = effectiveDate(a), dateB = effectiveDate(b);
    if (dateA != dateB)
        return dateA < dateB;
    return a.title < b.title;
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

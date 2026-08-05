// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "ReversedChatModel.h"

ReversedChatModel::ReversedChatModel(QObject *parent)
    : QAbstractProxyModel(parent)
{
}

void ReversedChatModel::setSourceModel(QAbstractItemModel *model)
{
    if (model == sourceModel())
        return;
    // Bracketed the way QIdentityProxyModel brackets its own: every row this
    // model has is about to mean something else, and the view has to be told
    // before the translations are unhooked rather than after.
    beginResetModel();
    if (sourceModel())
        disconnect(sourceModel(), nullptr, this, nullptr);
    QAbstractProxyModel::setSourceModel(model);
    connectSource();
    endResetModel();
}

void ReversedChatModel::connectSource()
{
    QAbstractItemModel *src = sourceModel();
    if (!src)
        return;

    // An insert at the source tail is a prepend here, and the range has to be
    // worked out from the count before the insert - which is the count the
    // source still reports while "about to" is running, and not the one it
    // reports afterwards.
    connect(src, &QAbstractItemModel::rowsAboutToBeInserted, this, [this](const QModelIndex &parent, int first, int last) {
        if (parent.isValid())
            return;
        const int before = sourceRowCount();
        const int added = last - first + 1;
        // Row `first` ends up `before - first` places from the new end, and the
        // block runs from there towards the older messages.
        const int proxyFirst = before - first;
        beginInsertRows(QModelIndex(), proxyFirst, proxyFirst + added - 1);
    });
    connect(src, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &parent, int, int) {
        if (parent.isValid())
            return;
        endInsertRows();
    });

    connect(src, &QAbstractItemModel::rowsAboutToBeRemoved, this, [this](const QModelIndex &parent, int first, int last) {
        if (parent.isValid())
            return;
        const int before = sourceRowCount();
        beginRemoveRows(QModelIndex(), before - 1 - last, before - 1 - first);
    });
    connect(src, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex &parent, int, int) {
        if (parent.isValid())
            return;
        endRemoveRows();
    });

    connect(src, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles) {
        if (!topLeft.isValid() || !bottomRight.isValid())
            return;
        // The corners swap ends with the order.
        const QModelIndex from = mapFromSource(bottomRight);
        const QModelIndex to = mapFromSource(topLeft);
        if (from.isValid() && to.isValid())
            Q_EMIT dataChanged(from, to, roles);
    });

    connect(src, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        beginResetModel();
    });
    connect(src, &QAbstractItemModel::modelReset, this, [this]() {
        endResetModel();
    });

    // ChatModel neither moves rows nor rearranges itself, so these are here
    // only so that a source which grew the habit would not go unnoticed. A
    // reset is the honest answer: reversing a move means remapping every
    // persistent index, and there is no caller asking for it.
    connect(src, &QAbstractItemModel::rowsAboutToBeMoved, this, [this]() {
        beginResetModel();
    });
    connect(src, &QAbstractItemModel::rowsMoved, this, [this]() {
        endResetModel();
    });
    connect(src, &QAbstractItemModel::layoutAboutToBeChanged, this, [this]() {
        beginResetModel();
    });
    connect(src, &QAbstractItemModel::layoutChanged, this, [this]() {
        endResetModel();
    });
}

int ReversedChatModel::sourceRowCount() const
{
    return sourceModel() ? sourceModel()->rowCount() : 0;
}

int ReversedChatModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return sourceRowCount();
}

int ReversedChatModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return sourceModel() ? sourceModel()->columnCount() : 0;
}

QModelIndex ReversedChatModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() || row < 0 || row >= rowCount() || column < 0 || column >= columnCount())
        return {};
    return createIndex(row, column);
}

QModelIndex ReversedChatModel::parent(const QModelIndex &child) const
{
    Q_UNUSED(child)
    return {};
}

QHash<int, QByteArray> ReversedChatModel::roleNames() const
{
    return sourceModel() ? sourceModel()->roleNames() : QAbstractProxyModel::roleNames();
}

int ReversedChatModel::toSourceRow(int proxyRow) const
{
    const int n = sourceRowCount();
    if (proxyRow < 0 || proxyRow >= n)
        return -1;
    return n - 1 - proxyRow;
}

int ReversedChatModel::fromSourceRow(int sourceRow) const
{
    // Same arithmetic in both directions, but spelled twice: a caller reads
    // which way round it is going off the name, and one of the two is always
    // the wrong one to have picked.
    const int n = sourceRowCount();
    if (sourceRow < 0 || sourceRow >= n)
        return -1;
    return n - 1 - sourceRow;
}

QModelIndex ReversedChatModel::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid() || !sourceModel())
        return {};
    const int row = toSourceRow(proxyIndex.row());
    if (row < 0)
        return {};
    return sourceModel()->index(row, proxyIndex.column());
}

QModelIndex ReversedChatModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (!sourceIndex.isValid() || sourceIndex.parent().isValid())
        return {};
    const int row = fromSourceRow(sourceIndex.row());
    if (row < 0)
        return {};
    return index(row, sourceIndex.column());
}

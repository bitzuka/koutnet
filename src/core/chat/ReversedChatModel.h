// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#pragma once

#include <QAbstractProxyModel>
#include <QQmlEngine>

// Turns a chat round: row 0 is the newest message and the last row is the
// oldest. Roles are forwarded untouched.
//
// This exists for the timeline's ListView, which is laid out BottomToTop. A
// bottom-to-top view is what stops a long conversation from drifting under the
// reader: the newest message is the end of the content that the view is
// actually resting against, and it is built out of rows that have been created
// and measured, whereas the far end is a guess made from an average row height
// - and a list holding both one-word replies and pictures has no useful average.
// With the guess at the top, off the screen, nothing the reader is looking at
// moves when it is corrected.
//
// ChatModel itself is left oldest-first. It is append-only, HistoryManager
// round-trips it in that order and the tests read it in that order; inverting
// it at the source would be inverting the log.
//
// Row numbers here are the view's, not the model's, and the two must not be
// confused when one is handed to the other - see toSourceRow() and
// fromSourceRow(). MessageTimeline.qml is the only place that crosses the
// boundary, so that everything above it keeps speaking in ChatModel rows.
class ReversedChatModel : public QAbstractProxyModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ReversedChatModel(QObject *parent = nullptr);

    void setSourceModel(QAbstractItemModel *model) override;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;

    // Both are their own inverse, and both answer -1 for a row that is not in
    // the model - including every row of an empty one, so a caller that forgot
    // to check gets an index the view will refuse rather than row 0.
    Q_INVOKABLE int toSourceRow(int proxyRow) const;
    Q_INVOKABLE int fromSourceRow(int sourceRow) const;

private:
    void connectSource();
    int sourceRowCount() const;

    // Held from rowsAboutToBeInserted to rowsInserted, because the source's own
    // count has already changed by the time the second one arrives and the
    // proxy range has to be worked out from the count before the insert.
    int m_pendingSourceCount = 0;
};

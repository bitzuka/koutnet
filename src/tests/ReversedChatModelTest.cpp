// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// Tests for the proxy the timeline is laid out over.
//
// The whole point of this class is that an append at the tail of the chat has
// to come out as a prepend at row 0, with the begin/end pair naming that row and
// no other. Get it wrong by one and QQuickListView builds the right number of
// delegates against the wrong rows, which on screen is a conversation where
// every message is signed by the person who sent the one before it - and nothing
// crashes, so nothing says so.
//
// No GUI here: this is arithmetic over signals, and it is checked as such.

#include <QAbstractItemModelTester>
#include <QAbstractListModel>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include "../core/chat/ReversedChatModel.h"

namespace
{

// The smallest thing that can stand in for ChatModel: oldest first, append
// only, one role. Everything the proxy does is a statement about row numbers,
// so nothing about a real message is needed to provoke it.
class SourceList : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        BodyRole = Qt::UserRole + 1,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return m_rows.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
            return {};
        if (role != BodyRole && role != Qt::DisplayRole)
            return {};
        return m_rows.at(index.row());
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {{BodyRole, "body"}};
    }

    void append(const QString &body)
    {
        const int row = m_rows.size();
        beginInsertRows(QModelIndex(), row, row);
        m_rows.append(body);
        endInsertRows();
    }

    void insertAt(int row, const QString &body)
    {
        beginInsertRows(QModelIndex(), row, row);
        m_rows.insert(row, body);
        endInsertRows();
    }

    void insertBlock(int row, const QStringList &block)
    {
        beginInsertRows(QModelIndex(), row, row + block.size() - 1);
        for (int i = 0; i < block.size(); ++i)
            m_rows.insert(row + i, block.at(i));
        endInsertRows();
    }

    void removeAt(int row)
    {
        beginRemoveRows(QModelIndex(), row, row);
        m_rows.removeAt(row);
        endRemoveRows();
    }

    void resetWith(const QStringList &rows)
    {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    }

    void touch(int first, int last)
    {
        Q_EMIT dataChanged(index(first), index(last), {BodyRole});
    }

private:
    QStringList m_rows;
};

QStringList bodies(const QAbstractItemModel &model)
{
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i)
        out.append(model.data(model.index(i, 0), SourceList::BodyRole).toString());
    return out;
}

} // namespace

class ReversedChatModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void reversesTheOrder();
    void mapsRowsBothWaysAtTheEdges();
    void refusesRowsThatAreNotThere();
    void survivesWithNoSource();
    void survivesAnEmptySource();
    void appendArrivesAsAPrepend();
    void insertAtTheHeadArrivesAtTheTail();
    void insertsABlockInOnePiece();
    void removalMapsToTheOtherEnd();
    void dataChangedSwapsItsCorners();
    void followsASourceReset();
    void swappingTheSourceResets();
    void forwardsTheRoleNames();
};

void ReversedChatModelTest::reversesTheOrder()
{
    SourceList src;
    src.resetWith({QStringLiteral("oldest"), QStringLiteral("middle"), QStringLiteral("newest")});

    ReversedChatModel proxy;
    proxy.setSourceModel(&src);

    QCOMPARE(proxy.rowCount(), 3);
    QCOMPARE(bodies(proxy), QStringList({QStringLiteral("newest"), QStringLiteral("middle"), QStringLiteral("oldest")}));
}

void ReversedChatModelTest::mapsRowsBothWaysAtTheEdges()
{
    SourceList src;
    src.resetWith({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});

    ReversedChatModel proxy;
    proxy.setSourceModel(&src);

    // The two ends are what a jump and a reply-quote actually land on, so they
    // are the two that matter.
    QCOMPARE(proxy.toSourceRow(0), 3);
    QCOMPARE(proxy.toSourceRow(3), 0);
    QCOMPARE(proxy.fromSourceRow(0), 3);
    QCOMPARE(proxy.fromSourceRow(3), 0);

    for (int i = 0; i < 4; ++i) {
        QCOMPARE(proxy.fromSourceRow(proxy.toSourceRow(i)), i);
        QCOMPARE(proxy.mapToSource(proxy.index(i, 0)).row(), proxy.toSourceRow(i));
        QCOMPARE(proxy.mapFromSource(src.index(i)).row(), proxy.fromSourceRow(i));
    }
}

void ReversedChatModelTest::refusesRowsThatAreNotThere()
{
    SourceList src;
    src.resetWith({QStringLiteral("a"), QStringLiteral("b")});

    ReversedChatModel proxy;
    proxy.setSourceModel(&src);

    // -1 and not 0. A caller that did not check gets an index the view refuses
    // rather than a silent jump to the newest message.
    QCOMPARE(proxy.toSourceRow(-1), -1);
    QCOMPARE(proxy.toSourceRow(2), -1);
    QCOMPARE(proxy.fromSourceRow(-1), -1);
    QCOMPARE(proxy.fromSourceRow(2), -1);
    QVERIFY(!proxy.mapToSource(QModelIndex()).isValid());
    QVERIFY(!proxy.mapFromSource(QModelIndex()).isValid());
}

void ReversedChatModelTest::survivesWithNoSource()
{
    ReversedChatModel proxy;
    QAbstractItemModelTester tester(&proxy);

    QCOMPARE(proxy.rowCount(), 0);
    QCOMPARE(proxy.columnCount(), 0);
    QCOMPARE(proxy.toSourceRow(0), -1);
    QCOMPARE(proxy.fromSourceRow(0), -1);
    QVERIFY(!proxy.index(0, 0).isValid());
}

void ReversedChatModelTest::survivesAnEmptySource()
{
    SourceList src;
    ReversedChatModel proxy;
    proxy.setSourceModel(&src);
    QAbstractItemModelTester tester(&proxy);

    QCOMPARE(proxy.rowCount(), 0);
    QVERIFY(!proxy.index(0, 0).isValid());

    // The first message in a conversation is still a prepend, even though there
    // is nothing in front of it.
    QSignalSpy inserted(&proxy, &QAbstractItemModel::rowsInserted);
    src.append(QStringLiteral("first"));
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 0);
    QCOMPARE(inserted.first().at(2).toInt(), 0);
    QCOMPARE(proxy.rowCount(), 1);
}

void ReversedChatModelTest::appendArrivesAsAPrepend()
{
    SourceList src;
    src.resetWith({QStringLiteral("a"), QStringLiteral("b")});

    ReversedChatModel proxy;
    proxy.setSourceModel(&src);
    QAbstractItemModelTester tester(&proxy);

    QSignalSpy about(&proxy, &QAbstractItemModel::rowsAboutToBeInserted);
    QSignalSpy inserted(&proxy, &QAbstractItemModel::rowsInserted);

    src.append(QStringLiteral("c"));

    QCOMPARE(about.count(), 1);
    QCOMPARE(inserted.count(), 1);
    // Row 0 and only row 0, in both halves of the pair.
    QCOMPARE(about.first().at(1).toInt(), 0);
    QCOMPARE(about.first().at(2).toInt(), 0);
    QCOMPARE(inserted.first().at(1).toInt(), 0);
    QCOMPARE(inserted.first().at(2).toInt(), 0);

    QCOMPARE(bodies(proxy), QStringList({QStringLiteral("c"), QStringLiteral("b"), QStringLiteral("a")}));
}

void ReversedChatModelTest::insertAtTheHeadArrivesAtTheTail()
{
    SourceList src;
    src.resetWith({QStringLiteral("a"), QStringLiteral("b")});

    ReversedChatModel proxy;
    proxy.setSourceModel(&src);
    QAbstractItemModelTester tester(&proxy);

    QSignalSpy inserted(&proxy, &QAbstractItemModel::rowsInserted);
    src.insertAt(0, QStringLiteral("older"));

    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 2);
    QCOMPARE(inserted.first().at(2).toInt(), 2);
    QCOMPARE(bodies(proxy), QStringList({QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("older")}));
}

void ReversedChatModelTest::insertsABlockInOnePiece()
{
    SourceList src;
    src.resetWith({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});

    ReversedChatModel proxy;
    proxy.setSourceModel(&src);
    QAbstractItemModelTester tester(&proxy);

    QSignalSpy inserted(&proxy, &QAbstractItemModel::rowsInserted);

    // Two rows at source 1 and 2 at once. Afterwards the source reads
    // a x y b c d, so reversed it reads d c b y x a and the pair is at proxy
    // rows 3 and 4 - the range worked out from the count before the insert, not
    // after, which is the one that is easy to get wrong by the size of the block.
    src.insertBlock(1, {QStringLiteral("x"), QStringLiteral("y")});

    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 3);
    QCOMPARE(inserted.first().at(2).toInt(), 4);
    QCOMPARE(bodies(proxy),
             QStringList({QStringLiteral("d"),
                          QStringLiteral("c"),
                          QStringLiteral("b"),
                          QStringLiteral("y"),
                          QStringLiteral("x"),
                          QStringLiteral("a")}));
}

void ReversedChatModelTest::removalMapsToTheOtherEnd()
{
    SourceList src;
    src.resetWith({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    ReversedChatModel proxy;
    proxy.setSourceModel(&src);
    QAbstractItemModelTester tester(&proxy);

    QSignalSpy removed(&proxy, &QAbstractItemModel::rowsRemoved);

    // Unsending the newest message: source row 2, proxy row 0.
    src.removeAt(2);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.first().at(1).toInt(), 0);
    QCOMPARE(removed.first().at(2).toInt(), 0);
    QCOMPARE(bodies(proxy), QStringList({QStringLiteral("b"), QStringLiteral("a")}));

    // And the oldest: source row 0, now the last proxy row.
    src.removeAt(0);
    QCOMPARE(removed.count(), 2);
    QCOMPARE(removed.at(1).at(1).toInt(), 1);
    QCOMPARE(removed.at(1).at(2).toInt(), 1);
    QCOMPARE(bodies(proxy), QStringList({QStringLiteral("b")}));
}

void ReversedChatModelTest::dataChangedSwapsItsCorners()
{
    SourceList src;
    src.resetWith({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e")});

    ReversedChatModel proxy;
    proxy.setSourceModel(&src);
    QAbstractItemModelTester tester(&proxy);

    QSignalSpy changed(&proxy, &QAbstractItemModel::dataChanged);
    src.touch(1, 3);

    QCOMPARE(changed.count(), 1);
    const auto topLeft = changed.first().at(0).value<QModelIndex>();
    const auto bottomRight = changed.first().at(1).value<QModelIndex>();
    QCOMPARE(topLeft.row(), 1);
    QCOMPARE(bottomRight.row(), 3);
    QVERIFY(topLeft.row() <= bottomRight.row());
    QCOMPARE(changed.first().at(2).value<QList<int>>(), QList<int>({SourceList::BodyRole}));
}

void ReversedChatModelTest::followsASourceReset()
{
    SourceList src;
    src.resetWith({QStringLiteral("a"), QStringLiteral("b")});

    ReversedChatModel proxy;
    proxy.setSourceModel(&src);
    QAbstractItemModelTester tester(&proxy);

    QSignalSpy about(&proxy, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy done(&proxy, &QAbstractItemModel::modelReset);

    src.resetWith({QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")});

    QCOMPARE(about.count(), 1);
    QCOMPARE(done.count(), 1);
    QCOMPARE(bodies(proxy), QStringList({QStringLiteral("z"), QStringLiteral("y"), QStringLiteral("x")}));

    // A conversation that was emptied leaves nothing behind.
    src.resetWith({});
    QCOMPARE(proxy.rowCount(), 0);
}

void ReversedChatModelTest::swappingTheSourceResets()
{
    SourceList first;
    first.resetWith({QStringLiteral("a")});
    SourceList second;
    second.resetWith({QStringLiteral("p"), QStringLiteral("q")});

    ReversedChatModel proxy;
    proxy.setSourceModel(&first);

    QSignalSpy done(&proxy, &QAbstractItemModel::modelReset);
    proxy.setSourceModel(&second);
    QVERIFY(done.count() >= 1);
    QCOMPARE(bodies(proxy), QStringList({QStringLiteral("q"), QStringLiteral("p")}));

    QAbstractItemModelTester tester(&proxy);

    // The chat that was switched away from must not still be driving the view.
    QSignalSpy inserted(&proxy, &QAbstractItemModel::rowsInserted);
    first.append(QStringLiteral("b"));
    QCOMPARE(inserted.count(), 0);

    second.append(QStringLiteral("r"));
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 0);
}

void ReversedChatModelTest::forwardsTheRoleNames()
{
    SourceList src;
    ReversedChatModel proxy;
    proxy.setSourceModel(&src);

    // A renamed role here is every delegate property coming back empty, and
    // QML says nothing about it.
    QCOMPARE(proxy.roleNames(), src.roleNames());
}

QTEST_MAIN(ReversedChatModelTest)

#include "ReversedChatModelTest.moc"

// SPDX-License-Identifier: GPL-3.0-or-later
#include "playbackguard.h"

#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>
#include <QTreeView>

#include <array>

// Isolated Qt-only metaobjects; the host driver verifies real widget identities.
namespace Fooyin {
class FyWidget : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class PlaylistWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class QueueViewer : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class LibraryTreeWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
namespace Filters {
class FilterWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
}
}
using namespace Fooyin::SessionLock;

class Probe : public QObject
{
public:
    QEvent::Type watched{};
    bool seen{false};
    bool eventFilter(QObject*, QEvent* event) override
    {
        if(event->type() == watched) { seen = true; return true; }
        return false;
    }
    bool reaches(QWidget* widget, QEvent& event)
    {
        watched = event.type(); seen = false;
        widget->installEventFilter(this);
        QCoreApplication::sendEvent(widget, &event);
        widget->removeEventFilter(this);
        return seen;
    }
};

class PlaybackGuardTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void activationAndBrowsing()
    {
        Fooyin::PlaylistWidget playlist;
        Fooyin::QueueViewer queue;
        Fooyin::LibraryTreeWidget library;
        Fooyin::Filters::FilterWidget filter;
        PlaybackGuard guard;
        RecoveryJournal journal{QString{}};
        QString error;
        QVERIFY(guard.enter(journal, error));
        Probe probe;
        const std::array<QWidget*, 4> roots{&playlist, &queue, &library, &filter};
        for(QWidget* root : roots) {
            QTreeView view{root}; // First input to a late view is protected.
            for(auto* target : {root, static_cast<QWidget*>(&view), view.viewport()}) {
                for(auto phase : {QEvent::ShortcutOverride, QEvent::KeyPress, QEvent::KeyRelease}) {
                    for(auto key : {Qt::Key_Enter, Qt::Key_Return}) {
                        for(auto modifier : {Qt::NoModifier, Qt::ControlModifier, Qt::ShiftModifier}) {
                            QKeyEvent enter{phase, key, modifier};
                            QVERIFY(!probe.reaches(target, enter));
                            QVERIFY(enter.isAccepted());
                        }
                    }
                    QKeyEvent browse{phase, Qt::Key_Down, Qt::NoModifier};
                    QVERIFY(probe.reaches(target, browse));
                }
            }
            for(auto phase : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonDblClick}) {
                QMouseEvent middle{phase, QPointF{2, 2}, QPointF{2, 2}, Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier};
                QVERIFY(!probe.reaches(view.viewport(), middle));
            }
            QMouseEvent select{QEvent::MouseButtonPress, QPointF{2, 2}, QPointF{2, 2}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
            QVERIFY(probe.reaches(view.viewport(), select));
            QMouseEvent move{QEvent::MouseMove, QPointF{20, 20}, QPointF{20, 20}, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
            QVERIFY(probe.reaches(view.viewport(), move)); // Selection drag is deliverable.
            view.viewport()->setAcceptDrops(true);
            QMimeData mime;
            QDragEnterEvent drop{{2, 2}, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier};
            drop.acceptProposedAction();
            QVERIFY(!probe.reaches(view.viewport(), drop));
            QVERIFY(!drop.isAccepted());
        }
    }

    void preservesTreeExpansionAndSelection()
    {
        Fooyin::LibraryTreeWidget library;
        QTreeView tree{&library};
        QStandardItemModel model;
        auto* parent = new QStandardItem{QStringLiteral("Parent")};
        parent->appendRow(new QStandardItem{QStringLiteral("Child")});
        model.appendRow(parent);
        tree.setModel(&model);
        library.resize(400, 300); tree.resize(400, 300); library.show();
        const auto index = model.index(0, 0);
        QSignalSpy activation{&tree, &QTreeView::doubleClicked};
        PlaybackGuard guard;
        RecoveryJournal journal{QString{}}; QString error;
        QVERIFY(guard.enter(journal, error));
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, tree.visualRect(index).center());
        QVERIFY(tree.selectionModel()->isSelected(index));
        for(bool expanded : {true, false}) {
            QTest::mouseDClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, tree.visualRect(index).center());
            QCOMPARE(tree.isExpanded(index), expanded);
        }
        QCOMPARE(activation.count(), 0);
        tree.setExpandsOnDoubleClick(false);
        QTest::mouseDClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, tree.visualRect(index).center());
        QVERIFY(!tree.isExpanded(index));
        QTest::keyClick(&tree, Qt::Key_Right);
        QVERIFY(tree.isExpanded(index));
    }

    void restoresDragAndFindBarInput()
    {
        Fooyin::PlaylistWidget playlist;
        QTreeView view{&playlist}; view.setDragEnabled(true);
        QLineEdit find{&playlist};
        QLineEdit inlineEditor{view.viewport()};
        RecoveryJournal journal{QString{}}; QString error;
        Probe probe;
        QKeyEvent enter{QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier};
        {
            PlaybackGuard guard;
            QVERIFY(guard.enter(journal, error));
            QVERIFY(guard.enter(journal, error));
            QVERIFY(!view.dragEnabled());
            QVERIFY(!probe.reaches(&find, enter));
            QVERIFY(probe.reaches(&inlineEditor, enter));
            QTest::keyClicks(&find, "find");
            QCOMPARE(find.text(), QStringLiteral("find"));
            QTreeView late{&playlist}; late.setDragEnabled(true);
            QKeyEvent browse{QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier};
            QVERIFY(probe.reaches(&late, browse));
            QVERIFY(!late.dragEnabled());
            late.setViewport(new QWidget);
            QVERIFY(probe.reaches(late.viewport(), browse));
            QVERIFY(guard.leave(error));
            QVERIFY(view.dragEnabled()); QVERIFY(late.dragEnabled());
            QVERIFY(probe.reaches(&find, enter));
            QVERIFY(guard.enter(journal, error));
        }
        QVERIFY(view.dragEnabled());
        QVERIFY(probe.reaches(&find, enter));
    }
};
QTEST_MAIN(PlaybackGuardTest)
#include "playbackguardtest.moc"

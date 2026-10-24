// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgetinputfilter.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTest>
#include <QTreeView>
#include <QWheelEvent>

// This executable links Qt only. These metaobject fixtures model audited class
// names without defining lookalike classes in a process linked to the real host.
// The separate real-host smoke check validates the assumptions against fooyin.
namespace Fooyin {
class FyWidget : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class PlaylistWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class PlaylistTabs : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class PlaylistBox : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class PlaylistManagerWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class PlaylistOrganiser : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class QueueViewer : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class LibraryTreeWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class SearchWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class SeekBar : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class TrackSlider : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
namespace Filters {
class FilterWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
}
namespace WaveBar {
class WaveBarWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class WaveSeekBar : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
}
namespace Spectrogram {
class SpectrogramWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class SpectrogramSeekBar : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class LiveSpectrogramView : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
}
} // namespace Fooyin

using Filter = Fooyin::SessionLock::WidgetInputFilter;
using Surface = Filter::Surface;
using Role = Filter::Role;

class ReceiverProbe : public QObject
{
public:
    int seen{0};
    QEvent::Type watched{QEvent::ShortcutOverride};
    bool eventFilter(QObject*, QEvent* event) override
    {
        if(event->type() == watched || Filter::isMutationDrop(event)) {
            ++seen;
            return true;
        }
        return false;
    }
};

class WidgetInputFilterTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void discoversSurfacesAndNestedBoundaries()
    {
        Fooyin::PlaylistTabs tabs;
        Fooyin::PlaylistWidget playlist{&tabs};
        Fooyin::QueueViewer queue{&tabs};
        Fooyin::LibraryTreeWidget library{&tabs};
        Fooyin::Filters::FilterWidget filter{&tabs};
        Fooyin::SearchWidget search{&library};
        Fooyin::SeekBar seek;
        Fooyin::WaveBar::WaveBarWidget wave;
        Fooyin::Spectrogram::SpectrogramWidget spectrogram;
        Fooyin::PlaylistBox box;
        Fooyin::PlaylistManagerWidget manager;
        Fooyin::PlaylistOrganiser organiser;
        const std::pair<QWidget*, Surface> targets[] = {
            {&tabs, Surface::Tabs}, {&playlist, Surface::Playlist}, {&queue, Surface::Queue},
            {&library, Surface::LibraryTree}, {&filter, Surface::Filter}, {&search, Surface::Search},
            {&seek, Surface::SeekBar}, {&wave, Surface::WaveBar}, {&spectrogram, Surface::Spectrogram},
            {&box, Surface::Switcher}, {&manager, Surface::Manager}, {&organiser, Surface::Organiser}
        };
        for(const auto& [root, expected] : targets) {
            QWidget child{root};
            QTreeView view{&child};
            QCOMPARE(Filter::surface(root), expected);
            QCOMPARE(Filter::surface(view.viewport()), expected);
            QCOMPARE(Filter::role(root), Role::Root);
            QCOMPARE(Filter::role(&view), Role::ItemView);
            QCOMPARE(Filter::role(view.viewport()), Role::Viewport);
            QCOMPARE(Filter::role(view.header()->viewport()), Role::Header);
            QCOMPARE(Filter::role(view.verticalScrollBar()), Role::ScrollBar);
            QLineEdit editor{view.viewport()};
            QCOMPARE(Filter::role(&editor), Role::TextEntry);
            QWidget accessory{&view};
            QCOMPARE(Filter::role(&accessory), Role::Other);
        }
        Fooyin::FyWidget unknown{&tabs};
        QTreeView unknownView{&unknown};
        QCOMPARE(Filter::surface(unknownView.viewport()), Surface::Unknown);
        unknown.setObjectName(QStringLiteral("Fooyin::QueueViewer"));
        QCOMPARE(Filter::surface(&unknown), Surface::Unknown); // Object names aren't identities.
        QWidget moving{&queue};
        QCOMPARE(Filter::surface(&moving), Surface::Queue);
        moving.setParent(&search);
        QCOMPARE(Filter::surface(&moving), Surface::Search);
        moving.setParent(nullptr);
        QCOMPARE(Filter::surface(&moving), Surface::Unknown);
        QCOMPARE(Filter::surface(nullptr), Surface::Unknown);
        QCOMPARE(Filter::role(nullptr), Role::Other);
    }

    void activationCandidatesPreserveEditorsAndBrowsing()
    {
        Fooyin::QueueViewer queue;
        QTreeView view{&queue};
        QLineEdit editor{view.viewport()};
        QMenu menu{&queue};
        for(auto type : {QEvent::KeyPress, QEvent::KeyRelease, QEvent::ShortcutOverride}) {
            for(int key : {Qt::Key_Return, Qt::Key_Enter}) {
                QKeyEvent enter{type, key, Qt::ControlModifier};
                for(auto* target : {static_cast<QWidget*>(&queue), static_cast<QWidget*>(&view), view.viewport()})
                    QVERIFY(Filter::isActivation(target, &enter));
                for(auto* target : {static_cast<QWidget*>(&editor), static_cast<QWidget*>(&menu),
                                    static_cast<QWidget*>(view.header()), static_cast<QWidget*>(view.verticalScrollBar())})
                    QVERIFY(!Filter::isActivation(target, &enter));
            }
            for(int key : {Qt::Key_Down, Qt::Key_Right, Qt::Key_Space, Qt::Key_F, Qt::Key_C, Qt::Key_Delete}) {
                QKeyEvent browse{type, key, Qt::NoModifier};
                QVERIFY(!Filter::isActivation(view.viewport(), &browse));
            }
        }
        for(auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonDblClick}) {
            for(auto button : {Qt::LeftButton, Qt::MiddleButton, Qt::RightButton}) {
                QMouseEvent event{type, QPointF{2, 2}, QPointF{2, 2}, button, button, Qt::NoModifier};
                QCOMPARE(Filter::isActivation(view.viewport(), &event),
                         button == Qt::MiddleButton || (button == Qt::LeftButton && type == QEvent::MouseButtonDblClick));
                QVERIFY(!Filter::isActivation(&editor, &event));
            }
        }
        Fooyin::SearchWidget search;
        QLineEdit text{&search};
        QKeyEvent enter{QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier};
        QVERIFY(Filter::isSearchSubmit(&text, &enter));
        QVERIFY(!Filter::isActivation(&text, &enter));
        QVERIFY(!Filter::isSearchSubmit(&editor, &enter));
        QKeyEvent copy{QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier};
        QVERIFY(!Filter::isSearchSubmit(&text, &copy));
    }

    void seekCandidatesStayOnActualControls()
    {
        Fooyin::SeekBar seek;
        Fooyin::TrackSlider slider{&seek};
        Fooyin::WaveBar::WaveBarWidget wave;
        Fooyin::WaveBar::WaveSeekBar waveSlider{&wave};
        Fooyin::Spectrogram::SpectrogramWidget spectrogram;
        Fooyin::Spectrogram::SpectrogramSeekBar spectroSlider{&spectrogram};
        Fooyin::Spectrogram::LiveSpectrogramView live{&spectrogram};
        QWidget label{&seek};
        QWheelEvent wheel{QPointF{2, 2}, QPointF{2, 2}, {}, QPoint{0, 120}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false};
        for(QWidget* control : {static_cast<QWidget*>(&slider), static_cast<QWidget*>(&waveSlider),
                                 static_cast<QWidget*>(&spectroSlider)}) {
            QCOMPARE(Filter::role(control), Role::SeekControl);
            QVERIFY(Filter::isSeekInput(control, &wheel));
            for(auto type : {QEvent::KeyPress, QEvent::KeyRelease, QEvent::ShortcutOverride}) {
                QKeyEvent arrow{type, Qt::Key_Right, Qt::NoModifier};
                QVERIFY(Filter::isSeekInput(control, &arrow));
                QKeyEvent home{type, Qt::Key_Home, Qt::NoModifier};
                QCOMPARE(Filter::isSeekInput(control, &home), control == &slider);
                QKeyEvent escape{type, Qt::Key_Escape, Qt::NoModifier};
                QVERIFY(!Filter::isSeekInput(control, &escape));
                QKeyEvent tab{type, Qt::Key_Tab, Qt::NoModifier};
                QVERIFY(!Filter::isSeekInput(control, &tab));
            }
            QMouseEvent release{QEvent::MouseButtonRelease, QPointF{2, 2}, QPointF{2, 2},
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
            QVERIFY(Filter::isSeekInput(control, &release)); // Even if press predates start().
            QMouseEvent move{QEvent::MouseMove, QPointF{2, 2}, QPointF{2, 2},
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
            QVERIFY(Filter::isSeekInput(control, &move));
            QMouseEvent hover{QEvent::MouseMove, QPointF{2, 2}, QPointF{2, 2},
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier};
            QVERIFY(!Filter::isSeekInput(control, &hover));
            QMouseEvent cancel{QEvent::MouseButtonPress, QPointF{2, 2}, QPointF{2, 2},
                               Qt::RightButton, Qt::RightButton, Qt::NoModifier};
            QVERIFY(!Filter::isSeekInput(control, &cancel));
        }
        for(QWidget* other : {static_cast<QWidget*>(&seek), static_cast<QWidget*>(&label), static_cast<QWidget*>(&live)})
            QVERIFY(!Filter::isSeekInput(other, &wheel));
    }

    void dispatchOrderingDynamicLifetimeAndAcceptance()
    {
        Fooyin::QueueViewer queue;
        ReceiverProbe probe;
        int calls = 0;
        auto filter = std::make_unique<Filter>([&](QWidget* widget, Surface, QEvent* event) {
            ++calls;
            return Filter::isActivation(widget, event) || Filter::isMutationDrop(event);
        });
        filter->start();
        filter->start();
        for(int i = 0; i < 2; ++i) {
            QTreeView view{&queue}; // Late/replaced viewport, no event-loop scan.
            view.setViewport(new QWidget);
            view.viewport()->setAcceptDrops(true);
            view.viewport()->installEventFilter(&probe);
            QKeyEvent enter{QEvent::ShortcutOverride, Qt::Key_Return, Qt::NoModifier};
            enter.ignore();
            const int before = calls;
            QCoreApplication::sendEvent(view.viewport(), &enter);
            QCOMPARE(calls, before + 1);
            QVERIFY(enter.isAccepted());
            QCOMPARE(probe.seen, 0);
            QMimeData mime;
            QDragEnterEvent drag{QPoint{2, 2}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
            drag.acceptProposedAction();
            QCoreApplication::sendEvent(view.viewport(), &drag);
            QVERIFY(!drag.isAccepted());
            QCOMPARE(probe.seen, 0);
            // Qt gates drag-move/drop delivery on a successful drag enter, so
            // their candidate classification is checked directly too.
            QDragMoveEvent move{QPoint{2, 2}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
            QDropEvent drop{QPointF{2, 2}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
            QVERIFY(Filter::isMutationDrop(&move));
            QVERIFY(Filter::isMutationDrop(&drop));
            QEvent leave{QEvent::DragLeave};
            QVERIFY(!Filter::isMutationDrop(&leave));
            QEvent paint{QEvent::User};
            const int beforePaint = calls;
            QCoreApplication::sendEvent(&view, &paint);
            QCOMPARE(calls, beforePaint);
        }
        queue.installEventFilter(&probe);
        probe.watched = QEvent::KeyPress;
        filter->stop(); filter->stop();
        QKeyEvent enter{QEvent::KeyPress, Qt::Key_Enter, Qt::NoModifier};
        QCoreApplication::sendEvent(&queue, &enter);
        QCOMPARE(probe.seen, 1);
        filter->start();
        filter.reset(); // Destruction while filtering detaches synchronously.
        QCoreApplication::sendEvent(&queue, &enter);
        QCOMPARE(probe.seen, 2);
    }

    void browsingActuallyWorksWithCandidateFilter()
    {
        Fooyin::LibraryTreeWidget library;
        QTreeView view{&library};
        QStandardItemModel model;
        auto* parent = new QStandardItem{QStringLiteral("Parent")};
        parent->appendRow(new QStandardItem{QStringLiteral("Child")});
        model.appendRow(parent);
        model.appendRow(new QStandardItem{QStringLiteral("Other")});
        view.setModel(&model);
        QLineEdit text{&library};
        Filter filter{[](QWidget* widget, Surface, QEvent* event) { return Filter::isActivation(widget, event); }};
        filter.start();
        view.setCurrentIndex(model.index(0, 0));
        QTest::keyClick(&view, Qt::Key_Right);
        QVERIFY(view.isExpanded(model.index(0, 0)));
        QTest::keyClick(&view, Qt::Key_Down);
        QCOMPARE(view.currentIndex(), model.index(0, 0, model.index(0, 0)));
        QTest::keyClicks(&text, "query");
        QCOMPARE(text.text(), QStringLiteral("query"));
        QTest::keyClick(&text, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(&text, "search");
        QCOMPARE(text.text(), QStringLiteral("search"));
    }
};

QTEST_MAIN(WidgetInputFilterTest)
#include "widgetinputfiltertest.moc"

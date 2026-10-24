// SPDX-License-Identifier: GPL-3.0-or-later
#include "playbackchecks.h"

#include <core/player/playercontroller.h>
#include <gui/fywidget.h>
#include <gui/guiconstants.h>
#include <gui/widgetprovider.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <memory>
#include <array>
#include <set>

namespace {
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

class DragSeed : public QObject
{
    bool eventFilter(QObject*, QEvent* event) override
    {
        if(event->type() != QEvent::DragEnter) return false;
        static_cast<QDragEnterEvent*>(event)->acceptProposedAction();
        return true;
    }
};

bool checkRoot(QWidget* root, Fooyin::ActionManager* actions)
{
    Probe probe;
    auto* view = root->findChild<QAbstractItemView*>();
    if(!view) return false;
    const std::array<QWidget*, 3> targets{root, view, view->viewport()};
    for(auto* target : targets) {
        for(auto phase : {QEvent::ShortcutOverride, QEvent::KeyPress, QEvent::KeyRelease}) {
            for(auto modifier : {Qt::NoModifier, Qt::ControlModifier, Qt::ShiftModifier}) {
                for(auto key : {Qt::Key_Return, Qt::Key_Enter}) {
                    QKeyEvent enter{phase, key, modifier};
                    if(probe.reaches(target, enter) || !enter.isAccepted()) return false;
                }
            }
        }
    }
    for(auto phase : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonDblClick}) {
        QMouseEvent middle{phase, QPointF{2, 2}, QPointF{2, 2}, Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier};
        if(probe.reaches(view->viewport(), middle)) return false;
    }
    QMouseEvent activation{QEvent::MouseButtonDblClick, QPointF{2, 2}, QPointF{2, 2},
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    if(probe.reaches(view->viewport(), activation)) return false;
    QMouseEvent select{QEvent::MouseButtonPress, QPointF{2, 2}, QPointF{2, 2},
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QMouseEvent move{QEvent::MouseMove, QPointF{20, 20}, QPointF{20, 20},
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
    QKeyEvent browse{QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier};
    QWheelEvent scroll{QPointF{2, 2}, QPointF{2, 2}, {}, QPoint{0, 120}, Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false};
    QContextMenuEvent menu{QContextMenuEvent::Mouse, {2, 2}, {2, 2}};
    if(!probe.reaches(view->viewport(), select) || !probe.reaches(view->viewport(), move)
       || !probe.reaches(view, browse) || !probe.reaches(view->viewport(), scroll)
       || !probe.reaches(root, menu) || view->dragEnabled()) return false;

    view->viewport()->setAcceptDrops(true);
    for(const auto& format : {QStringLiteral("application/x-fooyin-queuevieweritems"),
                             QString::fromLatin1(Fooyin::Constants::Mime::QueueTracks),
                             QString::fromLatin1(Fooyin::Constants::Mime::Tracks),
                             QString::fromLatin1(Fooyin::Constants::Mime::TrackIds), QStringLiteral("text/uri-list")}) {
        QMimeData mime; mime.setData(format, QByteArrayLiteral("test"));
        for(auto action : {Qt::CopyAction, Qt::MoveAction}) {
            QDragEnterEvent drag{QPoint{2, 2}, action, &mime, Qt::LeftButton, Qt::NoModifier};
            drag.acceptProposedAction();
            if(probe.reaches(view->viewport(), drag) || drag.isAccepted()) return false;
            // QApplication gates drag moves/drops after a rejected enter. Call
            // through a test-only earlier application filter to seed delivery,
            // modelling a drag accepted before lock entry, then ensure
            // each subsequent phase is rejected by the production filter.
            QDragMoveEvent moveDrop{QPoint{2, 2}, action, &mime, Qt::LeftButton, Qt::NoModifier};
            QDropEvent drop{QPointF{2, 2}, action, &mime, Qt::LeftButton, Qt::NoModifier};
            DragSeed seed;
            qApp->installEventFilter(&seed);
            QCoreApplication::sendEvent(view->viewport(), &drag);
            qApp->removeEventFilter(&seed);
            if(!drag.isAccepted()) return false;
            moveDrop.acceptProposedAction(); drop.acceptProposedAction();
            if(probe.reaches(view->viewport(), moveDrop) || moveDrop.isAccepted()) return false;
            if(probe.reaches(view->viewport(), drop) || drop.isAccepted()) return false;
        }
    }

    // Verify actual source dispatch, including host refreshes. Safe registered
    // copy/search/select-all sources must not be mistaken for anonymous commands.
    std::set<QAction*> registered;
    std::set<Fooyin::Id> contexts{Fooyin::Constants::Context::Global, Fooyin::Constants::Context::TrackSelection};
    for(auto* context : root->findChildren<Fooyin::WidgetContext*>(QString{}, Qt::FindDirectChildrenOnly)) {
        const auto ids = context->context(); contexts.insert(ids.begin(), ids.end());
    }
    for(auto* command : actions->commands()) {
        for(const auto& context : contexts) {
            auto* source = command->actionForContext(context);
            registered.insert(source);
            if(source && source->parent() == root) {
                const auto id = command->id().name();
                if(id == QLatin1StringView{Fooyin::Constants::Actions::SelectAll} && !source->isEnabled()) return false;
                using namespace Fooyin::Constants::Actions;
                if(id == QLatin1StringView{AddToQueue} || id == QLatin1StringView{QueueNext}
                   || id == QLatin1StringView{SendToQueue} || id == QLatin1StringView{RemoveFromQueue}
                   || id == QLatin1StringView{Remove} || id == QLatin1StringView{Clear}
                   || id == QLatin1StringView{SortReverse} || id == QLatin1StringView{SortRandomise}
                   || id.startsWith(QLatin1StringView{"Edit.Sort.Preset."})) {
                    int triggers = 0;
                    QObject receiver;
                    QObject::connect(source, &QAction::triggered, &receiver, [&]() { ++triggers; });
                    source->setEnabled(true);
                    source->trigger();
                    if(triggers || source->isEnabled()) return false;
                }
            }
        }
    }
    int anonymous = 0;
    for(auto* action : root->findChildren<QAction*>(QString{}, Qt::FindDirectChildrenOnly)) {
        if(registered.contains(action)) continue;
        ++anonymous;
        int triggered = 0;
        QObject receiver;
        QObject::connect(action, &QAction::triggered, &receiver, [&]() { ++triggered; });
        action->setEnabled(true);
        action->trigger();
        if(triggered || action->isEnabled()) return false;
    }
    if(!root->inherits("Fooyin::Filters::FilterWidget") && anonymous == 0) return false;
    // Deliver actual activation without a receiver probe as well. The populated
    // queue's second row would dequeue/reorder/start playback if it reached the
    // host. Its model is real, and the caller checks controller state afterward.
    QKeyEvent enter{QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier};
    QCoreApplication::sendEvent(root, &enter);
    if(root->inherits("Fooyin::QueueViewer")) {
        view->resize(400, 300);
        view->doItemsLayout();
        const auto index = view->model()->index(1, 0);
        const auto rect = view->visualRect(index);
        if(!index.isValid() || rect.isEmpty()) return false;
        view->setCurrentIndex(index);
        QMouseEvent play{QEvent::MouseButtonDblClick, QPointF{rect.center()}, QPointF{rect.center()},
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QCoreApplication::sendEvent(view->viewport(), &play);
    }
    // First input to a replacement viewport still uses production guards.
    view->setViewport(new QWidget);
    return !probe.reaches(view->viewport(), activation);
}
} // namespace

bool checkPlaybackInputs(Fooyin::WidgetProvider* provider, Fooyin::ActionManager* actions,
                         Fooyin::PlayerController* player, QWidget* playlist)
{
    const auto queue = player->playbackQueue().items();
    const auto upcoming = player->upcomingPlaylistTrack();
    const auto current = player->currentPlaylistTrack();
    const auto position = player->currentPosition();
    const auto stopAfter = player->stopAfterQueueItemId();
    if(!checkRoot(playlist, actions)) { qCritical("Playlist production playback checks failed"); return false; }
    for(int replacement = 0; replacement < 2; ++replacement) {
        for(const auto& key : {QStringLiteral("PlaybackQueue"), QStringLiteral("LibraryTree"), QStringLiteral("LibraryFilter")}) {
            std::unique_ptr<Fooyin::FyWidget> root{provider->createWidget(key)};
            if(!root || !checkRoot(root.get(), actions)) {
                qCritical() << "Production playback checks failed:" << key; return false;
            }
        }
    }
    if(player->playbackQueue().items() != queue || player->upcomingPlaylistTrack() != upcoming
       || player->currentPlaylistTrack() != current || player->currentPosition() != position
       || player->stopAfterQueueItemId() != stopAfter) return false;
    qInfo("Session Lock smoke: production playback gestures, direct menu actions, queue drops, and browsing verified");
    return true;
}

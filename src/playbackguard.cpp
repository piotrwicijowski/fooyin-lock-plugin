// SPDX-License-Identifier: GPL-3.0-or-later
#include "playbackguard.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTreeView>

namespace Fooyin::SessionLock {
namespace {
using Surface = WidgetInputFilter::Surface;
bool trackSurface(Surface surface)
{
    return surface == Surface::Playlist || surface == Surface::Queue
        || surface == Surface::LibraryTree || surface == Surface::Filter;
}

QAbstractItemView* containingView(QWidget* widget)
{
    for(auto* parent = widget; parent; parent = parent->parentWidget()) {
        if(auto* view = qobject_cast<QAbstractItemView*>(parent)) return view;
        if(parent->inherits("Fooyin::FyWidget")) break;
    }
    return nullptr;
}
} // namespace

PlaybackGuard::PlaybackGuard()
    : m_filter{[this](QWidget* widget, auto surface, QEvent* event) { return filter(widget, surface, event); }}
{ }
PlaybackGuard::~PlaybackGuard() { QString error; leave(error); }
QString PlaybackGuard::id() const { return QStringLiteral("playback-input"); }

bool PlaybackGuard::enter(RecoveryJournal&, QString& error)
{
    if(!qApp) { error = QStringLiteral("Application is unavailable"); return false; }
    m_filter.start();
    for(auto* widget : QApplication::allWidgets()) {
        if(trackSurface(WidgetInputFilter::surface(widget))
           && WidgetInputFilter::role(widget) == WidgetInputFilter::Role::ItemView) {
            inhibitDrag(qobject_cast<QAbstractItemView*>(widget));
        }
    }
    return true;
}

bool PlaybackGuard::leave(QString&)
{
    m_filter.stop();
    for(const auto& [_, state] : m_views) {
        if(state.view && !state.view->dragEnabled()) state.view->setDragEnabled(state.enabled);
    }
    m_views.clear();
    return true;
}

bool PlaybackGuard::recover(const RecoveryRecord&, QString& error)
{
    error = QStringLiteral("Playback input does not own persistent recovery records");
    return false;
}

void PlaybackGuard::inhibitDrag(QAbstractItemView* view)
{
    if(!view) return;
    if(!m_views.contains(view) || !m_views.at(view).view) m_views[view] = {view, view->dragEnabled()};
    else if(view->dragEnabled()) m_views[view].enabled = true;
    // Keep mouse moves deliverable for range/rubber-band selection. All audited
    // views consult this public property before entering their drag loop.
    view->setDragEnabled(false);
}

bool PlaybackGuard::filter(QWidget* widget, Surface surface, QEvent* event)
{
    if(!trackSurface(surface)) return false;
    const auto role = WidgetInputFilter::role(widget);
    if(role == WidgetInputFilter::Role::ItemView || role == WidgetInputFilter::Role::Viewport)
        inhibitDrag(containingView(widget)); // Includes first input to late/replaced views.
    if(WidgetInputFilter::isMutationDrop(event)) return true;

    // The built-in playlist find bar is an out-of-view QLineEdit. Return plays
    // its current match; Ctrl+Return queues it. Inline metadata editors are not
    // find bars, and retain their ordinary editing/commit input.
    if(surface == Surface::Playlist && qobject_cast<QLineEdit*>(widget) && !containingView(widget)
       && WidgetInputFilter::isKeyInput(event)) {
        const auto key = static_cast<QKeyEvent*>(event)->key();
        if(key == Qt::Key_Return || key == Qt::Key_Enter) return true;
    }
    if(!WidgetInputFilter::isActivation(widget, event)) return false;
    // Separate the library tree's browse-only double-click expansion from its
    // activation signal, without changing/saving the instance configuration.
    if(surface == Surface::LibraryTree && event->type() == QEvent::MouseButtonDblClick
       && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        if(auto* tree = qobject_cast<QTreeView*>(containingView(widget)); tree && tree->expandsOnDoubleClick()) {
            const auto position = static_cast<QMouseEvent*>(event)->position().toPoint();
            const auto index = tree->indexAt(tree->viewport()->mapFrom(widget, position));
            if(index.isValid() && tree->model()->hasChildren(index)) tree->setExpanded(index, !tree->isExpanded(index));
        }
    }
    return true;
}
} // namespace Fooyin::SessionLock

// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgetinputfilter.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextEdit>
#include <QWidget>

#include <utility>

namespace Fooyin::SessionLock {
namespace {
bool seekControl(const QWidget* widget)
{
    return widget->inherits("Fooyin::TrackSlider") || widget->inherits("Fooyin::WaveBar::WaveSeekBar")
        || widget->inherits("Fooyin::Spectrogram::SpectrogramSeekBar");
}

bool enterKey(const QEvent* event)
{
    return WidgetInputFilter::isKeyInput(event)
        && (static_cast<const QKeyEvent*>(event)->key() == Qt::Key_Return
            || static_cast<const QKeyEvent*>(event)->key() == Qt::Key_Enter);
}
} // namespace

WidgetInputFilter::WidgetInputFilter(Rule rule) : m_rule{std::move(rule)} { }
WidgetInputFilter::~WidgetInputFilter() { stop(); }

void WidgetInputFilter::start()
{
    if(!m_installed && qApp) {
        qApp->installEventFilter(this);
        m_installed = true;
    }
}

void WidgetInputFilter::stop()
{
    if(m_installed && qApp) {
        qApp->removeEventFilter(this);
    }
    m_installed = false;
}

WidgetInputFilter::Surface WidgetInputFilter::surface(const QWidget* widget)
{
    for(auto* parent = widget; parent; parent = parent->parentWidget()) {
        // The nearest surface wins: PlaylistTabs can contain a PlaylistWidget.
        if(parent->inherits("Fooyin::PlaylistWidget")) return Surface::Playlist;
        if(parent->inherits("Fooyin::PlaylistTabs")) return Surface::Tabs;
        if(parent->inherits("Fooyin::PlaylistBox")) return Surface::Switcher;
        if(parent->inherits("Fooyin::PlaylistManagerWidget")) return Surface::Manager;
        if(parent->inherits("Fooyin::PlaylistOrganiser")) return Surface::Organiser;
        if(parent->inherits("Fooyin::QueueViewer")) return Surface::Queue;
        if(parent->inherits("Fooyin::LibraryTreeWidget")) return Surface::LibraryTree;
        if(parent->inherits("Fooyin::Filters::FilterWidget")) return Surface::Filter;
        if(parent->inherits("Fooyin::SearchWidget")) return Surface::Search;
        if(parent->inherits("Fooyin::SeekBar")) return Surface::SeekBar;
        if(parent->inherits("Fooyin::WaveBar::WaveBarWidget")) return Surface::WaveBar;
        if(parent->inherits("Fooyin::Spectrogram::SpectrogramWidget")) return Surface::Spectrogram;
        // An unrelated layout widget inside playlist tabs is its own content
        // surface. Never inherit tab-header restrictions into library/search or
        // third-party widgets just because their outer container is a tab set.
        if(parent->inherits("Fooyin::FyWidget")) return Surface::Unknown;
    }
    return Surface::Unknown;
}

WidgetInputFilter::Role WidgetInputFilter::role(const QWidget* widget)
{
    for(auto* parent = widget; parent; parent = parent->parentWidget()) {
        if(qobject_cast<const QMenu*>(parent)) return Role::Menu;
        if(qobject_cast<const QLineEdit*>(parent) || qobject_cast<const QTextEdit*>(parent)
           || qobject_cast<const QPlainTextEdit*>(parent)) return Role::TextEntry;
        if(qobject_cast<const QScrollBar*>(parent)) return Role::ScrollBar;
        if(qobject_cast<const QHeaderView*>(parent)) return Role::Header;
        if(seekControl(parent)) return Role::SeekControl;
        if(auto* view = qobject_cast<const QAbstractItemView*>(parent)) {
            // Headers and scrollbars have already been excluded. A child editor
            // is TextEntry, not a playback-activation target.
            if(widget == view) return Role::ItemView;
            for(auto* child = widget; child && child != view; child = child->parentWidget()) {
                if(child == view->viewport()) return Role::Viewport;
            }
            return Role::Other;
        }
        if(parent->inherits("Fooyin::FyWidget")) return parent == widget ? Role::Root : Role::Other;
    }
    return Role::Other;
}

bool WidgetInputFilter::isPlaylistSurface(Surface surface)
{
    return surface == Surface::Playlist || surface == Surface::Tabs || surface == Surface::Switcher
        || surface == Surface::Manager || surface == Surface::Organiser;
}

bool WidgetInputFilter::isKeyInput(const QEvent* event)
{
    return event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease
        || event->type() == QEvent::ShortcutOverride;
}

bool WidgetInputFilter::isMutationDrop(const QEvent* event)
{
    return event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop;
}

bool WidgetInputFilter::isActivation(const QWidget* widget, const QEvent* event)
{
    const auto target = surface(widget);
    if(target != Surface::Playlist && target != Surface::Queue && target != Surface::LibraryTree
       && target != Surface::Filter) return false;
    const auto part = role(widget);
    if(part != Role::Root && part != Role::ItemView && part != Role::Viewport) return false;
    if(enterKey(event)) return true;
    switch(event->type()) {
        case QEvent::MouseButtonDblClick:
            return static_cast<const QMouseEvent*>(event)->button() == Qt::LeftButton
                || static_cast<const QMouseEvent*>(event)->button() == Qt::MiddleButton;
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease:
            return static_cast<const QMouseEvent*>(event)->button() == Qt::MiddleButton;
        default: return false;
    }
}

bool WidgetInputFilter::isSearchSubmit(const QWidget* widget, const QEvent* event)
{
    return surface(widget) == Surface::Search && enterKey(event)
        && (role(widget) == Role::Root || role(widget) == Role::TextEntry);
}

bool WidgetInputFilter::isSeekInput(const QWidget* widget, const QEvent* event)
{
    const auto target = surface(widget);
    if((target != Surface::SeekBar && target != Surface::WaveBar && target != Surface::Spectrogram)
       || role(widget) != Role::SeekControl) return false;
    if(isKeyInput(event)) {
        switch(static_cast<const QKeyEvent*>(event)->key()) {
            case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down: return true;
            case Qt::Key_Home: case Qt::Key_End: case Qt::Key_PageUp: case Qt::Key_PageDown:
                return target == Surface::SeekBar; // QSlider fallback keys.
            default: return false;
        }
    }
    switch(event->type()) {
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: case QEvent::MouseButtonDblClick:
            // Middle-click (with modifiers) sets, clears or toggles A–B repeat
            // directly, bypassing the registered playback actions.
            return static_cast<const QMouseEvent*>(event)->button() == Qt::LeftButton
                || static_cast<const QMouseEvent*>(event)->button() == Qt::MiddleButton;
        case QEvent::MouseMove:
            return static_cast<const QMouseEvent*>(event)->buttons().testAnyFlags(Qt::LeftButton | Qt::MiddleButton);
        case QEvent::Wheel: return true;
        default: return false;
    }
}

bool WidgetInputFilter::eventFilter(QObject* receiver, QEvent* event)
{
    // Avoid construction/destruction-time discovery and don't invoke input rules
    // for painting, model updates, timers or background playback notifications.
    switch(event->type()) {
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove: case QEvent::Wheel:
        case QEvent::KeyPress: case QEvent::KeyRelease: case QEvent::ShortcutOverride:
        case QEvent::DragEnter: case QEvent::DragMove: case QEvent::DragLeave: case QEvent::Drop:
        case QEvent::ContextMenu: break;
        default: return false;
    }
    auto* widget = qobject_cast<QWidget*>(receiver);
    if(!widget || !m_rule || !m_rule(widget, surface(widget), event)) return false;
    if(isMutationDrop(event)) event->ignore(); // Never advertise an accepted drop.
    else event->accept(); // In particular, claim a consumed ShortcutOverride.
    return true;
}
} // namespace Fooyin::SessionLock

// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgetinputfilter.h"

#include <QApplication>
#include <QWidget>

#include <utility>

namespace Fooyin::SessionLock {
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
        // An unrelated layout widget inside playlist tabs is its own content
        // surface. Never inherit tab-header restrictions into library/search or
        // third-party widgets just because their outer container is a tab set.
        if(parent->inherits("Fooyin::FyWidget")) return Surface::Unknown;
    }
    return Surface::Unknown;
}

bool WidgetInputFilter::eventFilter(QObject* receiver, QEvent* event)
{
    auto* widget = qobject_cast<QWidget*>(receiver);
    return widget && m_rule(widget, surface(widget), event);
}
} // namespace Fooyin::SessionLock

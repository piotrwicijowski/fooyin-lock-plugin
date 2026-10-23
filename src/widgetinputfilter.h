// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

#include <functional>

class QWidget;

namespace Fooyin::SessionLock {
// Application filters run before receiver filters/handlers. Resolve ancestry at
// dispatch, rather than caching constructor-time types or viewport pointers:
// newly created, reparented and replaced widgets are covered on their first input.
class WidgetInputFilter : public QObject
{
public:
    enum class Surface { Unknown, Playlist, Tabs, Switcher, Manager, Organiser };
    using Rule = std::function<bool(QWidget*, Surface, QEvent*)>;

    explicit WidgetInputFilter(Rule rule);
    ~WidgetInputFilter() override;
    void start();
    void stop();
    static Surface surface(const QWidget* widget);

protected:
    bool eventFilter(QObject* receiver, QEvent* event) override;

private:
    Rule m_rule;
    bool m_installed{false};
};
} // namespace Fooyin::SessionLock

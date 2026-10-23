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
    enum class Surface {
        Unknown, Playlist, Tabs, Switcher, Manager, Organiser,
        Queue, LibraryTree, Filter, Search, SeekBar, WaveBar, Spectrogram
    };
    enum class Role { Other, Root, ItemView, Viewport, Header, ScrollBar, TextEntry, Menu, SeekControl };
    using Rule = std::function<bool(QWidget*, Surface, QEvent*)>;

    explicit WidgetInputFilter(Rule rule);
    ~WidgetInputFilter() override;
    void start();
    void stop();
    static Surface surface(const QWidget* widget);
    static Role role(const QWidget* widget);
    static bool isPlaylistSurface(Surface surface);
    static bool isKeyInput(const QEvent* event);
    static bool isMutationDrop(const QEvent* event);
    // Candidates only: guards choose policy (e.g. preserve tree expansion when
    // separating double-click browsing from playback). No settings side effects
    // or configured single-click actions can be inferred from an input event.
    static bool isActivation(const QWidget* widget, const QEvent* event);
    static bool isSearchSubmit(const QWidget* widget, const QEvent* event);
    static bool isSeekInput(const QWidget* widget, const QEvent* event);

protected:
    bool eventFilter(QObject* receiver, QEvent* event) override;

private:
    Rule m_rule;
    bool m_installed{false};
};
} // namespace Fooyin::SessionLock

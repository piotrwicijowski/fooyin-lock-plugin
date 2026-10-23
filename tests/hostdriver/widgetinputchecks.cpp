// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgetinputchecks.h"
#include "widgetinputfilter.h"

#include <gui/fywidget.h>
#include <gui/widgetprovider.h>

#include <QAbstractItemView>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <memory>
#include <vector>

using Filter = Fooyin::SessionLock::WidgetInputFilter;
using Surface = Filter::Surface;
using Role = Filter::Role;

namespace {
class Probe : public QObject
{
public:
    QEvent::Type watched{QEvent::None};
    int seen{0};
    bool eventFilter(QObject*, QEvent* event) override
    {
        if(event->type() == watched) { ++seen; return true; }
        return false;
    }
    bool reaches(QWidget* widget, QEvent& event)
    {
        watched = event.type(); seen = 0;
        widget->installEventFilter(this);
        QCoreApplication::sendEvent(widget, &event);
        widget->removeEventFilter(this);
        return seen != 0;
    }
};

bool checkRoot(QWidget* root, Surface expected, bool active)
{
    if(!root || Filter::surface(root) != expected || Filter::role(root) != Role::Root) return false;
    Probe probe;
    QKeyEvent enter{QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier};
    const bool activation = expected == Surface::Queue || expected == Surface::LibraryTree || expected == Surface::Filter;
    const bool search = expected == Surface::Search;
    if(probe.reaches(root, enter) == (active && (activation || search))) return false;
    for(auto* child : root->findChildren<QWidget*>()) {
        if(child->inherits("Fooyin::SearchWidget") && Filter::surface(child) != Surface::Search) return false;
    }
    if(activation) {
        auto* view = root->findChild<QAbstractItemView*>();
        if(!view || Filter::surface(view->viewport()) != expected
           || Filter::role(view) != Role::ItemView || Filter::role(view->viewport()) != Role::Viewport
           || Filter::role(view->findChild<QHeaderView*>()) != Role::Header) return false;
        // Root and viewport paths must both run before receiver filters/signals.
        if(probe.reaches(view, enter) == active || probe.reaches(view->viewport(), enter) == active) return false;
        QMouseEvent doubleClick{QEvent::MouseButtonDblClick, QPointF{2, 2}, QPointF{2, 2},
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QMouseEvent middle{QEvent::MouseButtonRelease, QPointF{2, 2}, QPointF{2, 2},
                           Qt::MiddleButton, Qt::NoButton, Qt::NoModifier};
        if(probe.reaches(view->viewport(), doubleClick) == active
           || probe.reaches(view->viewport(), middle) == active) return false;
        QMouseEvent select{QEvent::MouseButtonPress, QPointF{2, 2}, QPointF{2, 2},
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QKeyEvent expand{QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier};
        QWheelEvent scroll{QPointF{2, 2}, QPointF{2, 2}, {}, QPoint{0, 120}, Qt::NoButton,
                           Qt::NoModifier, Qt::NoScrollPhase, false};
        if(!probe.reaches(view->viewport(), select) || !probe.reaches(view, expand)
           || !probe.reaches(view->viewport(), scroll) || !probe.reaches(view->verticalScrollBar(), enter)) return false;
        // Replacing the viewport must be covered on its first input, with no
        // queued scan or stale pointer to the previous viewport.
        view->setViewport(new QWidget);
        if(Filter::role(view->viewport()) != Role::Viewport || probe.reaches(view->viewport(), enter) == active) return false;
    }
    if(search) {
        auto* text = root->findChild<QLineEdit*>();
        if(!text || Filter::surface(text) != expected || Filter::role(text) != Role::TextEntry
           || probe.reaches(text, enter) == active) return false;
        QKeyEvent type{QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x")};
        QCoreApplication::sendEvent(text, &type);
        if(!text->text().contains(QLatin1Char{'x'})) return false;
        QKeyEvent copy{QEvent::ShortcutOverride, Qt::Key_C, Qt::ControlModifier};
        if(!probe.reaches(text, copy)) return false;
    }
    if(expected == Surface::SeekBar || expected == Surface::WaveBar || expected == Surface::Spectrogram) {
        if(expected == Surface::Spectrogram) {
            auto* fyRoot = qobject_cast<Fooyin::FyWidget*>(root);
            if(!fyRoot) return false;
            for(int mode : {0, 1}) { // Scrolling and stationary use a non-seeking live view.
                fyRoot->loadLayoutData({{QStringLiteral("PresentationMode"), mode}});
                QWidget* live = nullptr;
                for(auto* child : root->findChildren<QWidget*>()) {
                    if(child->inherits("Fooyin::Spectrogram::LiveSpectrogramView")) { live = child; break; }
                }
                QWheelEvent wheel{QPointF{2, 2}, QPointF{2, 2}, {}, QPoint{0, 120}, Qt::NoButton,
                                  Qt::NoModifier, Qt::NoScrollPhase, false};
                if(!live || Filter::role(live) == Role::SeekControl || !probe.reaches(live, wheel)) return false;
            }
            // This deletes the live subtree and creates a seekbar synchronously.
            fyRoot->loadLayoutData({{QStringLiteral("PresentationMode"), 2}});
        }
        QWidget* control = nullptr;
        for(auto* child : root->findChildren<QWidget*>()) {
            if(Filter::role(child) == Role::SeekControl) { control = child; break; }
        }
        if(!control) return false;
        QWheelEvent wheel{QPointF{2, 2}, QPointF{2, 2}, {}, QPoint{0, 120}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false};
        QKeyEvent seek{QEvent::ShortcutOverride, Qt::Key_Right, Qt::NoModifier};
        QMouseEvent release{QEvent::MouseButtonRelease, QPointF{2, 2}, QPointF{2, 2},
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
        if(Filter::surface(control) != expected || probe.reaches(control, wheel) == active
           || probe.reaches(control, seek) == active || probe.reaches(control, release) == active) return false;
        QKeyEvent tab{QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier};
        if(!probe.reaches(control, tab)) return false;
        // Spectrogram's live view and the time labels aren't seek controls.
        for(auto* child : root->findChildren<QWidget*>()) {
            if(child->inherits("Fooyin::Spectrogram::LiveSpectrogramView")
               && (!probe.reaches(child, wheel) || Filter::role(child) == Role::SeekControl)) return false;
        }
    }
    // Test a drop-capable descendant without letting the host act on its MIME
    // data. Ignore/accept state is part of the filter contract.
    QWidget destination{root};
    destination.setAcceptDrops(true);
    QMimeData mime;
    QDragEnterEvent drag{{2, 2}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
    drag.acceptProposedAction();
    if(probe.reaches(&destination, drag) == active || (active && drag.isAccepted())) return false;
    // Context-menu policy belongs to each guard, not the infrastructure.
    QContextMenuEvent context{QContextMenuEvent::Mouse, QPoint{2, 2}, QPoint{2, 2}};
    return probe.reaches(root, context);
}
} // namespace

bool checkWidgetInputInfrastructure(Fooyin::WidgetProvider* provider)
{
    std::vector<std::pair<QString, Surface>> surfaces{
        {QStringLiteral("PlaybackQueue"), Surface::Queue}, {QStringLiteral("LibraryTree"), Surface::LibraryTree},
        {QStringLiteral("LibraryFilter"), Surface::Filter}, {QStringLiteral("SearchBar"), Surface::Search},
        {QStringLiteral("SeekBar"), Surface::SeekBar}
    };
    if(qEnvironmentVariableIntValue("FOOYIN_LOCK_SMOKE_OPTIONAL_WIDGETS") == 1) {
        surfaces.emplace_back(QStringLiteral("WaveBar"), Surface::WaveBar);
        surfaces.emplace_back(QStringLiteral("Spectrogram"), Surface::Spectrogram);
    }
    // A test-only consumer exercises the reusable rules; tickets 007–009 own
    // production playback/seek/browsing policy and configuration suppression.
    Filter filter{[](QWidget* widget, Surface surface, QEvent* event) {
        return surface != Surface::Unknown && (Filter::isActivation(widget, event)
            || Filter::isSearchSubmit(widget, event) || Filter::isSeekInput(widget, event)
            || Filter::isMutationDrop(event));
    }};
    std::vector<std::unique_ptr<Fooyin::FyWidget>> existing;
    for(const auto& [key, surface] : surfaces) {
        existing.emplace_back(provider->createWidget(key));
        if(!existing.back() || !checkRoot(existing.back().get(), surface, false)) {
            qCritical() << "Session Lock smoke: existing widget audit failed:" << key;
            return false;
        }
    }
    filter.start(); filter.start();
    for(size_t i = 0; i < surfaces.size(); ++i) {
        const auto& [key, surface] = surfaces[i];
        if(!checkRoot(existing[i].get(), surface, true)) {
            qCritical() << "Session Lock smoke: active widget audit failed:" << key;
            return false;
        }
        for(int replacement = 0; replacement < 2; ++replacement) {
            std::unique_ptr<Fooyin::FyWidget> late{provider->createWidget(key)};
            if(!late || !checkRoot(late.get(), surface, true)) {
                qCritical() << "Session Lock smoke: late/replaced widget audit failed:" << key;
                return false;
            }
        }
    }
    // A nested search wins over library ancestry; an unknown FyWidget is a hard
    // content boundary, even inside a protected surface.
    std::unique_ptr<Fooyin::FyWidget> unknown{provider->createWidget(QStringLiteral("Spacer"))};
    if(!unknown) return false;
    unknown->setParent(existing.front().get());
    if(Filter::surface(unknown.get()) != Surface::Unknown) return false;
    QLineEdit text{unknown.get()};
    QKeyEvent enter{QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier};
    Probe probe;
    if(!probe.reaches(&text, enter)) return false;
    filter.stop(); filter.stop();
    for(size_t i = 0; i < surfaces.size(); ++i) {
        if(!checkRoot(existing[i].get(), surfaces[i].second, false)) return false;
    }
    if(surfaces.size() == 7) qInfo("Session Lock smoke: optional wavebar/spectrogram discovery and input roles verified");
    qInfo("Session Lock smoke: queue/library/filter/search/seek discovery, rules, ordering, and teardown verified");
    return true;
}

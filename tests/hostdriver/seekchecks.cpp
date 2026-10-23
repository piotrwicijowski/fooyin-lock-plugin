// SPDX-License-Identifier: GPL-3.0-or-later
#include "seekchecks.h"
#include "widgetinputfilter.h"

#include <core/player/playercontroller.h>
#include <gui/fywidget.h>
#include <gui/widgetprovider.h>

#include <QAction>
#include <QApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSlider>
#include <QWheelEvent>

#include <memory>
#include <vector>

using Filter = Fooyin::SessionLock::WidgetInputFilter;
namespace {
QWidget* control(Fooyin::FyWidget* root)
{
    for(auto* child : root->findChildren<QWidget*>())
        if(Filter::role(child) == Filter::Role::SeekControl) return child;
    if(root->inherits("Fooyin::Spectrogram::SpectrogramWidget"))
        root->loadLayoutData({{QStringLiteral("PresentationMode"), 2}});
    for(auto* child : root->findChildren<QWidget*>())
        if(Filter::role(child) == Filter::Role::SeekControl) return child;
    return nullptr;
}
void mouse(QWidget* widget, QEvent::Type phase)
{
    const QPointF point{widget->rect().center()};
    QMouseEvent event{phase, point, point, Qt::LeftButton,
                      phase == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier};
    QCoreApplication::sendEvent(widget, &event);
}
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
bool blocked(QWidget* widget)
{
    Probe probe;
    for(auto phase : {QEvent::ShortcutOverride, QEvent::KeyPress, QEvent::KeyRelease}) {
        for(auto key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down,
                        Qt::Key_Home, Qt::Key_End, Qt::Key_PageUp, Qt::Key_PageDown}) {
            QKeyEvent event{phase, key, Qt::ShiftModifier, {}, true};
            const bool seek = Filter::isSeekInput(widget, &event);
            if(probe.reaches(widget, event) == seek || (seek && !event.isAccepted())) {
                qCritical() << "Seek key delivery:" << phase << key << seek << widget->metaObject()->className();
                return false;
            }
        }
    }
    for(auto phase : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonDblClick}) {
        QMouseEvent event{phase, QPointF{20, 20}, QPointF{20, 20}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        if(probe.reaches(widget, event)) return false;
    }
    QMouseEvent move{QEvent::MouseMove, QPointF{30, 20}, QPointF{30, 20}, Qt::NoButton,
                      Qt::LeftButton, Qt::NoModifier};
    if(probe.reaches(widget, move)) return false;
    for(int delta : {-120, 120}) {
        QWheelEvent wheel{QPointF{2, 2}, QPointF{2, 2}, {}, QPoint{0, delta}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false};
        if(probe.reaches(widget, wheel)) return false;
        QCoreApplication::sendEvent(widget, &wheel); // Also exercise production handlers without a probe.
    }
    QKeyEvent right{QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier};
    QCoreApplication::sendEvent(widget, &right);
    mouse(widget, QEvent::MouseButtonPress);
    mouse(widget, QEvent::MouseButtonRelease);
    for(auto key : {Qt::Key_Tab, Qt::Key_Escape}) {
        QKeyEvent event{QEvent::KeyPress, key, Qt::NoModifier};
        if(!probe.reaches(widget, event)) { qCritical() << "Seek safe key:" << key; return false; }
    }
    QMouseEvent hover{QEvent::MouseMove, QPointF{30, 20}, QPointF{30, 20}, Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier};
    const bool tracking = widget->hasMouseTracking();
    widget->setMouseTracking(true); // Qt otherwise discards no-button moves before receiver filters.
    const bool delivered = probe.reaches(widget, hover);
    widget->setMouseTracking(tracking);
    return delivered;
}
}

bool checkSeekInputs(Fooyin::WidgetProvider* provider, Fooyin::PlayerController* player, QAction* toggle)
{
    // Model engine notifications through the installed public API, without
    // requesting playback or requiring a decoder/audio output device.
    Fooyin::Track track{QStringLiteral("https://example.invalid/session-lock-seek"), 0};
    track.setDuration(180000);
    player->commitCurrentTrack(track);
    player->syncPlayStateFromEngine(Fooyin::Player::PlayState::Paused);
    player->setCurrentTrackSeekable(true);
    QObject receiver;
    int seeks = 0;
    QObject::connect(player, &Fooyin::PlayerController::positionMoved, &receiver, [&](uint64_t) { ++seeks; });
    std::vector<QString> keys{QStringLiteral("SeekBar")};
    if(qEnvironmentVariableIntValue("FOOYIN_LOCK_SMOKE_OPTIONAL_WIDGETS") == 1) {
        keys.emplace_back(QStringLiteral("WaveBar"));
        keys.emplace_back(QStringLiteral("Spectrogram"));
    }
    std::vector<std::unique_ptr<Fooyin::FyWidget>> roots;
    for(const auto& key : keys) {
        roots.emplace_back(provider->createWidget(key));
        if(!roots.back()) return false;
        roots.back()->resize(400, 120);
        auto* target = control(roots.back().get());
        if(!target) return false;
        target->resize(400, 100);
        const int before = seeks;
        mouse(target, QEvent::MouseButtonPress);
        mouse(target, QEvent::MouseButtonRelease);
        // Wavebar requires decoded waveform data for mouse previews; wheel/keys
        // below still exercise its actual controller connection without decoding.
        if(key != QStringLiteral("WaveBar") && seeks == before) {
            qCritical() << "Seek fixture did not submit an unlocked mouse seek:" << key << target->isEnabled()
                        << player->currentTrack().isValid() << player->currentTrackSeekable();
            return false;
        }
        mouse(target, QEvent::MouseButtonPress); // Leave an active preview across entry.
    }
    toggle->trigger();
    if(!toggle->isChecked()) return false;
    const int before = seeks;
    for(const auto& root : roots) {
        if(auto* slider = qobject_cast<QSlider*>(control(root.get())); slider && slider->isSliderDown()) return false;
    }
    // Isolate entry cancellation from the per-input cancellation backstop:
    // unlock before any protected mouse input, then release the old previews.
    toggle->trigger();
    if(toggle->isChecked()) return false;
    for(const auto& root : roots) mouse(control(root.get()), QEvent::MouseButtonRelease);
    if(seeks != before) { qCritical("Entry did not cancel the active seek preview"); return false; }
    toggle->trigger();
    if(!toggle->isChecked()) return false;
    for(const auto& root : roots) {
        auto* target = control(root.get());
        if(!blocked(target)) { qCritical("Locked seek delivery failed"); return false; }
        if(auto* slider = qobject_cast<QSlider*>(target); slider && slider->isSliderDown()) return false;
    }
    player->setCurrentPosition(1200);
    if(auto* slider = qobject_cast<QSlider*>(control(roots.front().get())); !slider || slider->value() != 1200) {
        qCritical("Natural seekbar progress failed"); return false;
    }
    // First input to recreated controls is protected, with no discovery turn.
    for(const auto& key : keys) {
        std::unique_ptr<Fooyin::FyWidget> late{provider->createWidget(key)};
        if(!late || !control(late.get()) || !blocked(control(late.get()))) return false;
    }
    if(seeks != before || !player->currentTrackSeekable()) { qCritical("Locked seek/capability changed"); return false; }
    toggle->trigger();
    if(toggle->isChecked()) return false;
    for(const auto& root : roots) mouse(control(root.get()), QEvent::MouseButtonRelease);
    if(seeks != before) { qCritical("Cancelled seek revived"); return false; }
    for(const auto& root : roots) {
        QKeyEvent right{QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier};
        const int previous = seeks;
        QCoreApplication::sendEvent(control(root.get()), &right);
        if(seeks != previous + 1) { qCritical("Unlocked seek key failed"); return false; }
        QWheelEvent wheel{QPointF{2, 2}, QPointF{2, 2}, {}, QPoint{0, -120}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false};
        QCoreApplication::sendEvent(control(root.get()), &wheel);
        if(seeks != previous + 2) { qCritical("Unlocked seek wheel failed"); return false; }
    }
    toggle->trigger();
    Fooyin::Track nextTrack{QStringLiteral("https://example.invalid/session-lock-unseekable"), 0};
    nextTrack.setDuration(240000);
    player->commitCurrentTrack(nextTrack);
    player->setCurrentTrackSeekable(false);
    player->setCurrentPosition(2300);
    auto* slider = qobject_cast<QSlider*>(control(roots.front().get()));
    if(!toggle->isChecked() || !slider || slider->maximum() != 240000 || slider->value() != 2300
       || slider->isEnabled()) { qCritical("Unseekable track update failed"); return false; }
    for(const auto& key : keys) {
        std::unique_ptr<Fooyin::FyWidget> late{provider->createWidget(key)};
        if(!late || !control(late.get()) || !blocked(control(late.get()))) return false;
    }
    toggle->trigger();
    if(toggle->isChecked() || player->currentTrackSeekable() || slider->isEnabled()) return false;
    player->setCurrentTrackSeekable(true); // A subsequent engine update remains authoritative.
    if(!slider->isEnabled()) return false;
    roots.clear();
    player->syncPlayStateFromEngine(Fooyin::Player::PlayState::Stopped);
    qInfo("Session Lock smoke: production seeking, preview cancellation, progress, track capability, and recreation verified");
    return true;
}

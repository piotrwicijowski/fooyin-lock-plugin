// SPDX-License-Identifier: GPL-3.0-or-later
#include "seekguard.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>

#include <array>
#include <utility>

// Qt-only metaobject fixtures: never link host libraries into this executable.
// Model the audited preview/release/UngrabMouse protocol; the host driver checks
// production filtering against the actual widgets separately.
class PreviewControl : public QWidget
{
public:
    using QWidget::QWidget;
    bool preview{false};
    bool seekable{true};
    int requests{0};
    int position{0};
    int cancellations{0};
    void updatePosition(int value) { position = value; }
    bool event(QEvent* event) override
    {
        if(event->type() == QEvent::UngrabMouse) { preview = false; ++cancellations; }
        return QWidget::event(event);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        if(seekable && event->button() == Qt::LeftButton) preview = true;
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if(seekable && event->button() == Qt::LeftButton && std::exchange(preview, false)) ++requests;
    }
    void wheelEvent(QWheelEvent*) override { if(seekable) ++requests; }
    void keyPressEvent(QKeyEvent* event) override
    {
        if(seekable && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right
                       || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) ++requests;
    }
};
namespace Fooyin {
class FyWidget : public QWidget { Q_OBJECT public: using QWidget::QWidget; };
class SeekBar : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class TrackSlider : public PreviewControl { Q_OBJECT public: using PreviewControl::PreviewControl; };
namespace WaveBar {
class WaveBarWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class WaveSeekBar : public PreviewControl { Q_OBJECT public: using PreviewControl::PreviewControl; };
}
namespace Spectrogram {
class SpectrogramWidget : public FyWidget { Q_OBJECT public: using FyWidget::FyWidget; };
class SpectrogramSeekBar : public PreviewControl { Q_OBJECT public: using PreviewControl::PreviewControl; };
}
}

using namespace Fooyin::SessionLock;
namespace {
void mouse(QWidget* widget, QEvent::Type phase, Qt::MouseButton button = Qt::LeftButton)
{
    QMouseEvent event{phase, QPointF{20, 20}, QPointF{20, 20}, button,
                      phase == QEvent::MouseButtonRelease ? Qt::NoButton : button, Qt::NoModifier};
    QCoreApplication::sendEvent(widget, &event);
}
class Probe : public QObject
{
public:
    int inputs{0};
    bool eventFilter(QObject*, QEvent* event) override
    {
        if(WidgetInputFilter::isKeyInput(event) || event->type() == QEvent::Wheel
           || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
           || event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::MouseMove) {
            ++inputs;
            return true;
        }
        return false;
    }
};
}

class SeekGuardTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void cancelsPreLockGestureAndRestoresInteraction()
    {
        QTemporaryDir directory;
        RecoveryJournal journal{directory.filePath(QStringLiteral("recovery.json"))};
        Fooyin::SeekBar seek;
        Fooyin::TrackSlider slider{&seek};
        Fooyin::WaveBar::WaveBarWidget wave;
        Fooyin::WaveBar::WaveSeekBar waveSlider{&wave};
        Fooyin::Spectrogram::SpectrogramWidget spectrogram;
        Fooyin::Spectrogram::SpectrogramSeekBar spectroSlider{&spectrogram};
        const std::array<PreviewControl*, 3> controls{&slider, &waveSlider, &spectroSlider};
        SeekGuard guard;
        QString error;
        for(auto* control : controls) {
            mouse(control, QEvent::MouseButtonPress);
            QVERIFY(control->preview);
        }
        QVERIFY(guard.enter(journal, error));
        for(auto* control : controls) {
            QVERIFY(!control->preview); // Synchronous, even without a later release/tick.
            QVERIFY(control->cancellations > 0);
            mouse(control, QEvent::MouseButtonRelease);
            QTest::mouseClick(control, Qt::LeftButton);
            QTest::keyClick(control, Qt::Key_Right);
            QWheelEvent wheel{QPointF{2, 2}, QPointF{2, 2}, {}, QPoint{0, 120}, Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false};
            QCoreApplication::sendEvent(control, &wheel);
            QCOMPARE(control->requests, 0);
            control->updatePosition(9000); // Host updates are independent of input filtering.
            QCOMPARE(control->position, 9000);
            QVERIFY(control->isEnabled());
            QVERIFY(control->seekable);
        }
        QVERIFY(guard.leave(error));
        QVERIFY(guard.leave(error));
        for(auto* control : controls) {
            mouse(control, QEvent::MouseButtonRelease); // A cancelled preview cannot revive on unlock.
            QCOMPARE(control->requests, 0);
            QTest::mouseClick(control, Qt::LeftButton);
            QTest::keyClick(control, Qt::Key_Left);
            QCOMPARE(control->requests, 2);
            control->seekable = false;
        }
        QVERIFY(guard.enter(journal, error));
        QVERIFY(guard.leave(error));
        for(auto* control : controls) {
            QVERIFY(!control->seekable);
            QTest::mouseClick(control, Qt::LeftButton);
            QCOMPARE(control->requests, 2);
        }
    }

    void phasesLateReplacementAndScopedCleanup()
    {
        QTemporaryDir directory;
        RecoveryJournal journal{directory.filePath(QStringLiteral("recovery.json"))};
        Fooyin::SeekBar root;
        auto guard = std::make_unique<SeekGuard>();
        QString error;
        QVERIFY(guard->enter(journal, error));
        QVERIFY(guard->enter(journal, error));
        for(int replacement = 0; replacement < 2; ++replacement) {
            Fooyin::TrackSlider control{&root};
            Probe probe;
            control.installEventFilter(&probe);
            for(auto phase : {QEvent::KeyPress, QEvent::KeyRelease, QEvent::ShortcutOverride}) {
                for(auto key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down,
                                Qt::Key_Home, Qt::Key_End, Qt::Key_PageUp, Qt::Key_PageDown}) {
                    QKeyEvent event{phase, key, Qt::ControlModifier, {}, true};
                    QCoreApplication::sendEvent(&control, &event);
                    QVERIFY(event.isAccepted());
                }
            }
            for(auto phase : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonDblClick}) {
                mouse(&control, phase);
                for(auto modifiers : {Qt::KeyboardModifiers{}, Qt::KeyboardModifiers{Qt::ShiftModifier},
                                      Qt::KeyboardModifiers{Qt::ControlModifier},
                                      Qt::KeyboardModifiers{Qt::ShiftModifier | Qt::ControlModifier}}) {
                    QMouseEvent event{phase, QPointF{20, 20}, QPointF{20, 20}, Qt::MiddleButton,
                                      phase == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MiddleButton, modifiers};
                    QCoreApplication::sendEvent(&control, &event);
                }
            }
            QMouseEvent move{QEvent::MouseMove, QPointF{30, 20}, QPointF{30, 20}, Qt::NoButton,
                              Qt::LeftButton, Qt::NoModifier};
            QCoreApplication::sendEvent(&control, &move);
            QCOMPARE(probe.inputs, 0);
            // Non-seeking input remains available, including cancellation and focus traversal.
            mouse(&control, QEvent::MouseButtonPress, Qt::RightButton);
            QKeyEvent escape{QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier};
            QCoreApplication::sendEvent(&control, &escape);
            QCOMPARE(probe.inputs, 2);
        }
        // A control can arrive with an active gesture from an unrelated surface.
        Fooyin::TrackSlider moving;
        mouse(&moving, QEvent::MouseButtonPress);
        QVERIFY(moving.preview);
        moving.setParent(&root);
        mouse(&moving, QEvent::MouseButtonRelease);
        QVERIFY(!moving.preview);
        QCOMPARE(moving.requests, 0);
        guard.reset(); // Destruction while locked detaches; no retained widget pointers.
        QTest::mouseClick(&moving, Qt::LeftButton);
        QCOMPARE(moving.requests, 1);
    }
};
QTEST_MAIN(SeekGuardTest)
#include "seekguardtest.moc"

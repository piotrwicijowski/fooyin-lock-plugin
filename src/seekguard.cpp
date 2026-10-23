// SPDX-License-Identifier: GPL-3.0-or-later
#include "seekguard.h"

#include <QApplication>
#include <QWidget>

namespace Fooyin::SessionLock {
SeekGuard::SeekGuard()
    : m_filter{[](QWidget* widget, auto, QEvent* event) {
        if(!WidgetInputFilter::isSeekInput(widget, event)) return false;
        // Also clears a gesture on a late/reparented control, before its first
        // protected input. Never deliver a release to cancel: it commits a seek.
        cancelSeek(widget);
        return true;
    }}
{ }
SeekGuard::~SeekGuard() { QString error; leave(error); }
QString SeekGuard::id() const { return QStringLiteral("seek-input"); }

bool SeekGuard::enter(RecoveryJournal&, QString& error)
{
    if(!qApp) { error = QStringLiteral("Application is unavailable"); return false; }
    m_filter.start();
    for(auto* widget : QApplication::allWidgets()) {
        const auto surface = WidgetInputFilter::surface(widget);
        if((surface == WidgetInputFilter::Surface::SeekBar || surface == WidgetInputFilter::Surface::WaveBar
            || surface == WidgetInputFilter::Surface::Spectrogram)
           && WidgetInputFilter::role(widget) == WidgetInputFilter::Role::SeekControl) cancelSeek(widget);
    }
    return true;
}

void SeekGuard::cancelSeek(QWidget* widget)
{
    // Resolve the actual control even when input targets a generic descendant.
    while(widget->parentWidget()
          && WidgetInputFilter::role(widget->parentWidget()) == WidgetInputFilter::Role::SeekControl)
        widget = widget->parentWidget();
    if(QWidget::mouseGrabber() == widget) widget->releaseMouse();
    // All three audited controls handle UngrabMouse by clearing their preview
    // and tooltip without submitting a seek. This works without an actual grab
    // (keyboard lock toggle, offscreen platform, or a hidden control).
    QEvent cancel{QEvent::UngrabMouse};
    QCoreApplication::sendEvent(widget, &cancel);
}

bool SeekGuard::leave(QString&)
{
    m_filter.stop();
    return true;
}

bool SeekGuard::recover(const RecoveryRecord&, QString& error)
{
    error = QStringLiteral("Seek input does not own persistent recovery records");
    return false;
}
} // namespace Fooyin::SessionLock

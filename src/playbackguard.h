// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lockcoordinator.h"
#include "widgetinputfilter.h"

#include <QAbstractItemView>
#include <QPointer>

#include <map>

namespace Fooyin::SessionLock {
class PlaybackGuard : public LockGuard
{
public:
    PlaybackGuard();
    ~PlaybackGuard() override;
    QString id() const override;
    bool enter(RecoveryJournal& journal, QString& error) override;
    bool leave(QString& error) override;
    bool prepareRecovery(QString&) override { return true; }
    bool finishRecovery(QString& error) override { return leave(error); }
    bool recover(const RecoveryRecord& record, QString& error) override;

private:
    struct DragState { QPointer<QAbstractItemView> view; bool enabled; };
    void inhibitDrag(QAbstractItemView* view);
    bool filter(QWidget* widget, WidgetInputFilter::Surface surface, QEvent* event);
    WidgetInputFilter m_filter;
    std::map<QAbstractItemView*, DragState> m_views;
};
} // namespace Fooyin::SessionLock

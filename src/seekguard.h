// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lockcoordinator.h"
#include "widgetinputfilter.h"

namespace Fooyin::SessionLock {
class SeekGuard : public LockGuard
{
public:
    SeekGuard();
    ~SeekGuard() override;
    QString id() const override;
    bool enter(RecoveryJournal& journal, QString& error) override;
    bool leave(QString& error) override;
    bool prepareRecovery(QString&) override { return true; }
    bool finishRecovery(QString& error) override { return leave(error); }
    bool recover(const RecoveryRecord& record, QString& error) override;

private:
    static void cancelSeek(QWidget* widget);
    WidgetInputFilter m_filter;
};
} // namespace Fooyin::SessionLock

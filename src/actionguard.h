// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lockcoordinator.h"
#include "widgetinputfilter.h"

#include <QPointer>

#include <map>
#include <vector>

class QAction;
class QActionGroup;
class QMenu;

namespace Fooyin {
class ActionManager;
class Command;
}

namespace Fooyin::SessionLock {
class ActionGuard : public QObject, public LockGuard
{
    Q_OBJECT
public:
    explicit ActionGuard(ActionManager* actions);
    ~ActionGuard() override;

    [[nodiscard]] QString id() const override;
    bool enter(RecoveryJournal& journal, QString& error) override;
    bool leave(QString& error) override;
    bool prepareRecovery(QString&) override { return true; }
    bool finishRecovery(QString& error) override { return leave(error); }
    bool recover(const RecoveryRecord& record, QString& error) override;

private:
    struct Restriction
    {
        QPointer<QAction> action;
        QPointer<QActionGroup> originalGroup;
        QActionGroup* inhibitor;
    };

    void refresh();
    void protectDirectActions(QWidget* widget);
    void protectRepeatMenu(QMenu* menu);
    bool eventFilter(QObject* receiver, QEvent* event) override;
    void suppress(QAction* action);
    void release(QAction* action);
    static void restore(const Restriction& restriction);

    QPointer<ActionManager> m_actions;
    WidgetInputFilter m_directInput;
    std::map<QAction*, Restriction> m_restrictions;
    std::vector<QPointer<Command>> m_commands;
    QMetaObject::Connection m_registrationConnection;
    bool m_locked{false};
    bool m_refreshing{false};
};
} // namespace Fooyin::SessionLock

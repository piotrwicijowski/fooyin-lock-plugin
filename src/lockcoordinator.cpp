// SPDX-License-Identifier: GPL-3.0-or-later
#include "lockcoordinator.h"

#include <QScopedValueRollback>

#include <algorithm>
#include <utility>

namespace Fooyin::SessionLock {
LockCoordinator::LockCoordinator(QString recoveryPath, QObject* parent)
    : QObject{parent}
    , m_journal{std::move(recoveryPath)}
{ }

LockCoordinator::~LockCoordinator()
{
    shutdown();
}

bool LockCoordinator::addGuard(std::unique_ptr<LockGuard> guard)
{
    if(m_busy || m_state != State::Recovering || m_loaded || !guard || guard->id().isEmpty()
       || std::ranges::any_of(m_guards, [&](const auto& item) { return item->id() == guard->id(); })) {
        return false;
    }
    m_guards.push_back(std::move(guard));
    return true;
}

bool LockCoordinator::recover()
{
    if(m_busy || m_state == State::Shutdown || m_state == State::Locked) {
        return false;
    }
    const QScopedValueRollback busy{m_busy, true};
    changeState(State::Recovering);
    QString error;
    if(!m_loaded) {
        m_loaded = m_journal.load(error);
    }
    const bool success = m_loaded && restore(error);
    m_error.clear();
    changeState(success ? State::Unlocked : State::RecoveryRequired);
    if(!success) {
        report(error);
    }
    return success;
}

bool LockCoordinator::setLocked(bool locked)
{
    if(m_busy || m_state == State::Shutdown) {
        return false;
    }
    if((locked && m_state == State::Locked) || (!locked && m_state == State::Unlocked)) {
        return true;
    }
    if(m_state != State::Unlocked && m_state != State::Locked) {
        return false;
    }
    const QScopedValueRollback busy{m_busy, true};
    m_error.clear();
    changeState(locked ? State::Entering : State::Unlocking);
    QString error;
    if(locked) {
        for(const auto& guard : m_guards) {
            // Include the failing guard: entry can fail after partial changes.
            m_applied.push_back(guard.get());
            if(!guard->enter(m_journal, error)) {
                if(error.isEmpty()) {
                    error = QStringLiteral("Guard %1 could not apply restrictions").arg(guard->id());
                }
                QString rollbackError;
                const bool restored = restore(rollbackError);
                changeState(restored ? State::Unlocked : State::RecoveryRequired);
                report(QStringLiteral("Session Lock entry failed: %1%2")
                           .arg(error, rollbackError.isEmpty() ? QString{} : QStringLiteral("; ") + rollbackError));
                return false;
            }
        }
        changeState(State::Locked);
        return true;
    }
    const bool restored = restore(error);
    changeState(restored ? State::Unlocked : State::RecoveryRequired);
    if(!restored) {
        report(error);
    }
    return restored;
}

bool LockCoordinator::restore(QString& error)
{
    QStringList errors;
    // Reverse entry order; retain failed ephemeral cleanup for a later retry.
    for(size_t i = m_applied.size(); i > 0; --i) {
        QString detail;
        if(m_applied[i - 1]->leave(detail)) {
            m_applied.erase(m_applied.begin() + static_cast<std::ptrdiff_t>(i - 1));
        }
        else {
            errors.append(QStringLiteral("%1 cleanup failed: %2").arg(m_applied[i - 1]->id(), detail));
        }
    }
    const auto records = m_journal.records();
    for(auto it = records.crbegin(); it != records.crend(); ++it) {
        const auto guard = std::ranges::find_if(m_guards, [&](const auto& item) { return item->id() == it->guard; });
        QString detail;
        if(guard == m_guards.end()) {
            errors.append(QStringLiteral("Recovery handler unavailable: %1 (%2)").arg(it->guard, it->target));
        }
        else if(std::ranges::find(m_applied, guard->get()) != m_applied.end()) {
            errors.append(QStringLiteral("Recovery deferred until %1 enforcement is detached").arg(it->guard));
        }
        else if(!(*guard)->recover(*it, detail) || !m_journal.forget(*it, detail)) {
            errors.append(QStringLiteral("Recovery failed for %1: %2").arg(it->target, detail));
        }
    }
    error = errors.join(QStringLiteral("; "));
    return errors.isEmpty();
}

void LockCoordinator::shutdown()
{
    if(m_state == State::Shutdown || m_busy) {
        return;
    }
    const QScopedValueRollback busy{m_busy, true};
    // Do not attempt startup recovery before host readiness. The disk record
    // remains intact for the next startup if shutdown happens early.
    if(m_loaded) {
        QString error;
        if(!restore(error)) {
            report(error);
        }
    }
    changeState(State::Shutdown);
}

LockCoordinator::State LockCoordinator::state() const { return m_state; }
QString LockCoordinator::lastError() const { return m_error; }
void LockCoordinator::changeState(State state)
{
    if(m_state != state) {
        m_state = state;
        Q_EMIT stateChanged(state);
    }
}
void LockCoordinator::report(const QString& message)
{
    m_error = message;
    Q_EMIT failure(message);
}
} // namespace Fooyin::SessionLock

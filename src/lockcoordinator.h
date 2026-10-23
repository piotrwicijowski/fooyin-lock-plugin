// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "recoveryjournal.h"

#include <QObject>

#include <memory>
#include <vector>

namespace Fooyin::SessionLock {
class LockGuard
{
public:
    virtual ~LockGuard() = default;
    [[nodiscard]] virtual QString id() const = 0;
    virtual bool enter(RecoveryJournal& journal, QString& error) = 0;
    // Idempotently remove ephemeral changes, including partially applied entry.
    // Persistent originals are restored by recover(), NOT by leave().
    virtual bool leave(QString& error) = 0;
    // True only after durable restoration, or verified deletion of the exact
    // stable target. Unknown values/targets and unavailable services must fail.
    virtual bool recover(const RecoveryRecord& record, QString& error) = 0;
};

class LockCoordinator : public QObject
{
    Q_OBJECT
public:
    enum class State { Recovering, Unlocked, Entering, Locked, Unlocking, RecoveryRequired, Shutdown };
    Q_ENUM(State)

    explicit LockCoordinator(QString recoveryPath, QObject* parent = nullptr);
    ~LockCoordinator() override;

    bool addGuard(std::unique_ptr<LockGuard> guard);
    bool recover(); // Call only after the host has loaded settings and playlists.
    bool setLocked(bool locked);
    void shutdown();
    [[nodiscard]] State state() const;
    [[nodiscard]] QString lastError() const;

Q_SIGNALS:
    void stateChanged(Fooyin::SessionLock::LockCoordinator::State state);
    void failure(const QString& message);

private:
    bool restore(QString& error);
    void changeState(State state);
    void report(const QString& message);

    RecoveryJournal m_journal;
    std::vector<std::unique_ptr<LockGuard>> m_guards;
    std::vector<LockGuard*> m_applied;
    State m_state{State::Recovering};
    QString m_error;
    bool m_busy{false};
    bool m_loaded{false};
};
} // namespace Fooyin::SessionLock

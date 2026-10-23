// SPDX-License-Identifier: GPL-3.0-or-later
#include "actionguard.h"

#include <gui/guiconstants.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>

#include <QActionGroup>
#include <QApplication>
#include <QScopedValueRollback>

#include <algorithm>
#include <array>
#include <set>

namespace Fooyin::SessionLock {
namespace {
enum class Scope { None, All, Editing };

Scope scope(const QString& name)
{
    using namespace Constants::Actions;
    static constexpr std::array protectedIds{
        Stop, Play, Pause, PlayPause, Next, Previous, RandomTrack, RandomAlbum, PreviousAlbum, NextAlbum,
        PlaybackDefault, RepeatTrack, RepeatAlbum, RepeatPlaylist, ShuffleTracks, ShuffleAlbums, Random,
        StopAfterCurrent, StopAfterCurrentReset, PlaybackFollowsCursor,
        SeekForwardSmall, SeekForwardLarge, SeekBackwardSmall, SeekBackwardLarge,
        AddFiles, AddFolders, AddStreamUrl, NewPlaylist, NewAutoPlaylist, EditAutoPlaylist, RemovePlaylist,
        LoadPlaylist, ClearPlaylist, LockPlaylist, AddToCurrent, AddToActive, AddToPlaylist, SendToCurrent,
        SendToNew, AddToQueue, QueueNext, SendToQueue, RemoveFromQueue,
        "PlaylistOrganiser.NewGroup", "PlaylistOrganiser.SortAllPlaylists", "PlaylistOrganiser.SortGroupPlaylists"};
    if(std::ranges::any_of(protectedIds, [&name](const char* id) { return name == QLatin1StringView{id}; })
       || name == QLatin1StringView{SortRandomise} || name == QLatin1StringView{SortReverse}
       || name.startsWith(QLatin1StringView{"Edit.Sort.Preset."})) {
        return Scope::All;
    }
    static constexpr std::array editIds{Cut, Paste, Clear, Undo, Redo, Remove, Rename};
    return std::ranges::any_of(editIds, [&name](const char* id) { return name == QLatin1StringView{id}; })
               ? Scope::Editing : Scope::None;
}

bool editingContext(const Id& context)
{
    const auto name = context.name();
    return name.startsWith(QLatin1StringView{Constants::Context::Playlist})
           || name.startsWith(QLatin1StringView{"Context.QueueViewer."})
           || name.startsWith(QLatin1StringView{"Context.PlaylistOrganiser."});
}

void refreshProxy(Command* command)
{
    // Both methods refresh the proxy from its current source even if the
    // attribute is already in the requested state. Keep the attribute unchanged.
    if(command->hasAttribute(ProxyAction::UpdateText)) {
        command->setAttribute(ProxyAction::UpdateText);
    }
    else {
        command->removeAttribute(ProxyAction::UpdateText);
    }
}
} // namespace

ActionGuard::ActionGuard(ActionManager* actions)
    : m_actions{actions}
{ }

ActionGuard::~ActionGuard()
{
    QString error;
    leave(error);
}

QString ActionGuard::id() const
{
    return QStringLiteral("registered-actions");
}

bool ActionGuard::enter(RecoveryJournal&, QString& error)
{
    if(!m_actions) {
        error = tr("Action manager is unavailable");
        return false;
    }
    if(!m_locked) {
        m_locked = true;
        m_registrationConnection = connect(m_actions, &ActionManager::commandsChanged, this, &ActionGuard::refresh);
        refresh();
    }
    return true;
}

bool ActionGuard::leave(QString&)
{
    m_locked = false;
    disconnect(m_registrationConnection);
    for(const auto& command : m_commands) {
        if(command) {
            disconnect(command->action(), nullptr, this, nullptr);
        }
    }
    // Release all groups, then explicitly refresh proxies: a proxy may have
    // cached 'disabled' from its inhibited source, which is not host intent.
    for(const auto& [_, restriction] : m_restrictions) {
        restore(restriction);
    }
    m_restrictions.clear();
    for(const auto& command : m_commands) {
        if(command) {
            refreshProxy(command);
        }
    }
    m_commands.clear();
    return true;
}

bool ActionGuard::recover(const RecoveryRecord&, QString& error)
{
    error = tr("Registered actions do not own persistent recovery records");
    return false;
}

void ActionGuard::suppress(QAction* action)
{
    if(!action || m_restrictions.contains(action)) {
        return;
    }
    auto* inhibitor = new QActionGroup{this};
    inhibitor->setExclusive(false);
    inhibitor->setEnabled(false);
    m_restrictions.emplace(action, Restriction{action, action->actionGroup(), inhibitor});
    // Never call setEnabled on the source: Qt retains its explicit host value,
    // including a setEnabled(false) that produces no notification while locked.
    action->setActionGroup(inhibitor);
}

void ActionGuard::restore(const Restriction& restriction)
{
    if(auto* action = restriction.action.data(); action && action->actionGroup() == restriction.inhibitor) {
        if(restriction.originalGroup) {
            action->setActionGroup(restriction.originalGroup);
        }
        else {
            // Let Qt recompute from its *current* explicit value before removing
            // the group; removing a disabled group alone does not recompute it.
            restriction.inhibitor->setEnabled(true);
            action->setActionGroup(nullptr);
        }
    }
    delete restriction.inhibitor;
}

void ActionGuard::release(QAction* action)
{
    const auto it = m_restrictions.find(action);
    if(it != m_restrictions.end()) {
        const auto restriction = it->second;
        m_restrictions.erase(it);
        restore(restriction);
    }
}

void ActionGuard::refresh()
{
    if(!m_locked || !m_actions || m_refreshing) {
        return;
    }
    const QScopedValueRollback refreshing{m_refreshing, true};
    for(auto it = m_restrictions.begin(); it != m_restrictions.end();) {
        if(!it->second.action) {
            delete it->second.inhibitor;
            it = m_restrictions.erase(it);
        }
        else {
            ++it;
        }
    }
    std::erase_if(m_commands, [](const auto& command) { return !command; });

    // Command::context() is the current selection, not an enumeration of its
    // overrides. Find public WidgetContexts too, including inactive instances
    // and constructor-created contexts not yet added to ActionManager.
    std::set<Id> contexts{Constants::Context::Global, Constants::Context::TrackSelection};
    for(auto* widget : QApplication::allWidgets()) {
        if(auto* context = m_actions->contextObject(widget)) {
            const auto ids = context->context();
            contexts.insert(ids.begin(), ids.end());
        }
        for(auto* context : widget->findChildren<WidgetContext*>(QString{}, Qt::FindDirectChildrenOnly)) {
            const auto ids = context->context();
            contexts.insert(ids.begin(), ids.end());
        }
    }
    const auto commands = m_actions->commands();
    for(auto* command : commands) {
        const auto ids = command->context();
        contexts.insert(ids.begin(), ids.end());
    }
    for(auto* command : commands) {
        const auto protection = scope(command->id().name());
        if(protection == Scope::None) {
            continue;
        }
        auto* proxy = qobject_cast<ProxyAction*>(command->action());
        if(!proxy) {
            continue;
        }
        if(std::ranges::none_of(m_commands, [command](const auto& item) { return item == command; })) {
            m_commands.emplace_back(command);
            connect(proxy, &ProxyAction::currentActionChanged, this, &ActionGuard::refresh);
        }
        bool protectCurrent = protection == Scope::All;
        for(const auto& context : contexts) {
            if(protection == Scope::All || editingContext(context)) {
                if(auto* source = command->actionForContext(context)) {
                    suppress(source);
                    protectCurrent |= source == proxy->action();
                }
            }
        }
        if(protectCurrent) {
            suppress(proxy->action());
            suppress(proxy);
        }
        else {
            release(proxy);
            refreshProxy(command);
        }
    }
}
} // namespace Fooyin::SessionLock

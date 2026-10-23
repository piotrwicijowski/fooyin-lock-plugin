// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lockcoordinator.h"
#include "widgetinputfilter.h"

#include <core/playlist/playlisthandler.h>

#include <QAbstractItemView>
#include <QPointer>

#include <map>

namespace Fooyin::SessionLock {
class PlaylistGuard : public QObject, public LockGuard
{
    Q_OBJECT
public:
    PlaylistGuard(PlaylistHandler* playlists, std::weak_ptr<DbConnectionPool> pool);
    ~PlaylistGuard() override;
    QString id() const override;
    bool enter(RecoveryJournal& journal, QString& error) override;
    bool leave(QString& error) override;
    bool prepareRecovery(QString& error) override;
    bool finishRecovery(QString& error) override { return leave(error); }
    bool recover(const RecoveryRecord& record, QString& error) override;

    // Ticket 009 must suppress generated writes/remaps even for autoplaylists;
    // content locks alone are not an authorization or remapping boundary.
    [[nodiscard]] bool protects(const Playlist* playlist) const;
    [[nodiscard]] bool suppressesGeneratedWrites() const;

Q_SIGNALS:
    void protectionFailed(const QString& error);

private:
    struct Row { int dbId; QString marker; QByteArray properties; };
    PlaylistList allPlaylists() const;
    bool rows(std::vector<Row>& result, QString& error) const;
    bool syncDatabase(QString& error) const;
    bool save(Playlist* playlist, const QString& marker, QString& error);
    bool protect(Playlist* playlist, QString& error);
    bool filter(QWidget* widget, WidgetInputFilter::Surface surface, QEvent* event);
    void inhibitEditor(QAbstractItemView* view);

    QPointer<PlaylistHandler> m_playlists;
    std::weak_ptr<DbConnectionPool> m_pool;
    RecoveryJournal* m_journal{nullptr};
    WidgetInputFilter m_filter;
    std::vector<QMetaObject::Connection> m_connections;
    struct Editor { QPointer<QAbstractItemView> view; QAbstractItemView::EditTriggers triggers; };
    std::map<QAbstractItemView*, Editor> m_editors;
};
} // namespace Fooyin::SessionLock

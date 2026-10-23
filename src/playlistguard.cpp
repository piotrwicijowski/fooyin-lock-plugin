// SPDX-License-Identifier: GPL-3.0-or-later
#include "playlistguard.h"

#include <utils/database/dbconnectionprovider.h>

#include <QAbstractButton>
#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>
#include <set>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Fooyin::SessionLock {
namespace {
QString marker(const Playlist* playlist)
{
    return playlist->extraProperties().value(QString::fromLatin1(PlaylistIdentityProperty));
}

bool validMarker(const QString& value)
{
    const QUuid uuid{value};
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == value;
}

class CommittedReader
{
public:
    explicit CommittedReader(const QSqlDatabase& host)
        : m_name{QUuid::createUuid().toString()}, m_db{QSqlDatabase::cloneDatabase(host, m_name)}
    {
        m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    }
    ~CommittedReader()
    {
        m_db.close();
        m_db = {};
        QSqlDatabase::removeDatabase(m_name);
    }
    QSqlDatabase& db() { return m_db; }
private:
    QString m_name;
    QSqlDatabase m_db;
};

// Audited 0.13.1 extra-property encoding: Qt_6_0, quint32 count, QString pairs.
// Do not let the host's permissive decoder turn corrupt storage into 'deleted'.
bool decode(const QByteArray& bytes, QString& identity)
{
    if(bytes.isEmpty()) return true;
    QDataStream stream{bytes};
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 count{0};
    stream >> count;
    if(count > static_cast<quint32>(bytes.size() / 8)) return false;
    std::set<QString> keys;
    for(quint32 i = 0; i < count; ++i) {
        QString key, value;
        stream >> key >> value;
        if(stream.status() != QDataStream::Ok || !keys.insert(key).second) return false;
        if(key == QLatin1StringView{PlaylistIdentityProperty}) identity = value;
        if(key == QLatin1StringView{"core/locked"}) {
            if(value != QLatin1StringView{"1"}) return false; // Host removes this key when unlocked.
        }
    }
    return stream.status() == QDataStream::Ok && stream.atEnd();
}

bool syncPath(const QString& path, bool directory, QString& error)
{
#ifdef Q_OS_UNIX
    const auto encoded = QFile::encodeName(path);
    const int fd = ::open(encoded.constData(), O_RDONLY | (directory ? O_DIRECTORY : 0) | O_CLOEXEC);
    if(fd >= 0) {
        const bool ok = ::fsync(fd) == 0;
        ::close(fd);
        if(ok) return true;
    }
#else
    Q_UNUSED(directory)
#endif
    error = QStringLiteral("Cannot durably sync playlist storage: %1").arg(path);
    return false;
}
} // namespace

PlaylistGuard::PlaylistGuard(PlaylistHandler* playlists, std::weak_ptr<DbConnectionPool> pool)
    : m_playlists{playlists}, m_pool{std::move(pool)}
    , m_filter{[this](QWidget* widget, auto surface, QEvent* event) { return filter(widget, surface, event); }}
{ }

PlaylistGuard::~PlaylistGuard() { QString error; leave(error); }
QString PlaylistGuard::id() const { return QStringLiteral("playlists"); }
bool PlaylistGuard::protects(const Playlist* playlist) const { return m_journal && playlist; }
bool PlaylistGuard::suppressesGeneratedWrites() const { return m_journal != nullptr; }

PlaylistList PlaylistGuard::allPlaylists() const
{
    PlaylistList result;
    if(m_playlists) {
        for(const auto& list : {m_playlists->playlists(), m_playlists->removedPlaylists(),
                               m_playlists->pendingRemovedPlaylists()}) {
            for(auto* playlist : list) {
                if(std::ranges::find(result, playlist) == result.end()) result.push_back(playlist);
            }
        }
    }
    return result;
}

bool PlaylistGuard::rows(std::vector<Row>& result, QString& error) const
{
    const auto pool = m_pool.lock();
    if(!pool || !pool->hasThreadConnection()) {
        error = tr("Playlist database service is unavailable");
        return false;
    }
    const auto host = DbConnectionProvider{pool}.db();
    if(!host.isOpen() || host.driverName() != QStringLiteral("QSQLITE")) {
        error = tr("The playlist database is not an open SQLite database");
        return false;
    }
    // A separate read-only connection cannot mistake the host's uncommitted
    // transaction for a successful save, even if its void setter/save failed.
    CommittedReader reader{host};
    if(!reader.db().open()) {
        error = tr("Cannot read committed playlist storage: %1").arg(reader.db().lastError().text());
        return false;
    }
    QSqlQuery query{reader.db()};
    if(!query.exec(QStringLiteral("SELECT PlaylistID, ExtraProperties FROM Playlists"))) {
        error = tr("Cannot verify playlist storage: %1").arg(query.lastError().text());
        return false;
    }
    while(query.next()) {
        Row row{query.value(0).toInt(), {}, query.value(1).toByteArray()};
        if(!decode(row.properties, row.marker)) {
            error = tr("Malformed playlist properties in durable storage");
            return false;
        }
        result.push_back(std::move(row));
    }
    if(query.lastError().isValid()) {
        error = query.lastError().text();
        return false;
    }
    return true;
}

bool PlaylistGuard::syncDatabase(QString& error) const
{
    const auto pool = m_pool.lock();
    if(!pool || !pool->hasThreadConnection()) {
        error = tr("Playlist database service is unavailable");
        return false;
    }
    const auto path = DbConnectionProvider{pool}.db().databaseName();
    if(path.isEmpty() || !QFileInfo{path}.isFile()) {
        error = tr("Playlist storage is not a persistent local file");
        return false;
    }
    if(!syncPath(path, false, error)) return false;
    // Host commits are synchronous, but a WAL commit may not yet be in the main
    // file. Flush both, then directory entries, before acknowledging the journal.
    const auto wal = path + QStringLiteral("-wal");
    if(QFileInfo::exists(wal) && !syncPath(wal, false, error)) return false;
    auto directory = QFileInfo{path}.absoluteDir();
    do {
        if(!syncPath(directory.absolutePath(), true, error)) return false;
    } while(directory.cdUp());
    return true;
}

bool PlaylistGuard::save(Playlist* playlist, const QString& identity, QString& error)
{
    if(!m_playlists) { error = tr("Playlist handler is unavailable"); return false; }
    m_playlists->savePlaylist(playlist->id());
    std::vector<Row> stored;
    if(!rows(stored, error)) return false;
    const auto matches = std::ranges::count_if(stored, [&](const auto& row) { return row.marker == identity; });
    const auto found = std::ranges::find_if(stored, [&](const auto& row) {
        return row.dbId == playlist->dbId() && row.marker == identity
            && row.properties == playlist->serialiseExtraProperties();
    });
    if(matches != 1 || found == stored.end()) {
        error = tr("Playlist marker/lock persistence could not be confirmed: %1").arg(identity);
        return false;
    }
    return syncDatabase(error);
}

bool PlaylistGuard::protect(Playlist* playlist, QString& error)
{
    if(!playlist || playlist->isAutoPlaylist() || playlist->isLocked()) return true;
    if(playlist->isTemporary()) return true; // Not enumerated/persisted by the host.
    auto identity = marker(playlist);
    if(identity.isEmpty()) {
        identity = QUuid::createUuid().toString(QUuid::WithoutBraces);
        playlist->setExtraProperty(QString::fromLatin1(PlaylistIdentityProperty), identity);
    }
    const auto all = allPlaylists();
    if(!validMarker(identity) || std::ranges::count_if(all, [&](auto* item) { return marker(item) == identity; }) != 1) {
        error = tr("Invalid or ambiguous playlist recovery marker: %1").arg(identity);
        return false;
    }
    // Persist the stable marker BEFORE writing any recovery intent or lock.
    if(!save(playlist, identity, error)
       || !m_journal->remember({id(), QStringLiteral("playlist-lock"), identity, false, true}, error)) return false;
    m_playlists->setPlaylistLocked(playlist->id(), true);
    return playlist->isLocked() && save(playlist, identity, error);
}

bool PlaylistGuard::enter(RecoveryJournal& journal, QString& error)
{
    if(m_journal) return true;
    if(!m_playlists || !qApp) { error = tr("Playlist handler/application is unavailable"); return false; }
    // Verify storage even when every existing playlist is already locked.
    std::vector<Row> stored;
    if(!rows(stored, error)) return false;
    m_journal = &journal;
    // Cancel an unsent inline rename before focus changes can commit it. This
    // deliberately leaves general search/text editors alone.
    for(auto* widget : QApplication::allWidgets()) {
        if(widget->inherits("Fooyin::PopupLineEdit")
           && WidgetInputFilter::surface(widget) != WidgetInputFilter::Surface::Unknown) {
            QKeyEvent cancel{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
            QCoreApplication::sendEvent(widget, &cancel);
        }
    }
    m_filter.start();
    for(auto* widget : QApplication::allWidgets()) {
        if(WidgetInputFilter::surface(widget) == WidgetInputFilter::Surface::Manager) {
            if(auto* view = qobject_cast<QAbstractItemView*>(widget)) inhibitEditor(view);
        }
    }
    m_connections.push_back(connect(m_playlists, &PlaylistHandler::playlistAdded, this, [this](Playlist* playlist) {
        QString detail;
        if(!protect(playlist, detail)) Q_EMIT protectionFailed(detail);
    }));
    for(auto* playlist : m_playlists->playlists()) {
        if(!protect(playlist, error)) return false;
    }
    return true;
}

bool PlaylistGuard::leave(QString&)
{
    m_filter.stop();
    for(const auto& connection : m_connections) disconnect(connection);
    m_connections.clear();
    m_journal = nullptr;
    for(const auto& [_, editor] : m_editors) {
        if(editor.view && editor.view->editTriggers() == QAbstractItemView::NoEditTriggers)
            editor.view->setEditTriggers(editor.triggers);
    }
    m_editors.clear();
    return true;
}

bool PlaylistGuard::recover(const RecoveryRecord& record, QString& error)
{
    if(record.guard != id() || record.kind != QStringLiteral("playlist-lock") || !validMarker(record.target)
       || record.original.typeId() != QMetaType::Bool || record.temporary.typeId() != QMetaType::Bool
       || record.original.toBool() || !record.temporary.toBool()) {
        error = tr("Unsupported playlist recovery record");
        return false;
    }
    if(!m_playlists) { error = tr("Playlist handler is unavailable"); return false; }
    std::vector<Row> stored;
    if(!rows(stored, error)) return false;
    const auto all = allPlaylists();
    PlaylistList matches;
    for(auto* playlist : all) if(marker(playlist) == record.target) matches.push_back(playlist);
    const auto diskCount = std::ranges::count_if(stored, [&](const auto& row) { return row.marker == record.target; });
    if(matches.size() > 1 || diskCount > 1) { error = tr("Ambiguous playlist identity"); return false; }
    if(matches.empty()) {
        if(diskCount) { error = tr("Stored playlist identity is not loaded"); return false; }
        return syncDatabase(error); // Exact marker absent, never match a name/row ID.
    }
    auto* playlist = matches.front();
    if(playlist->isAutoPlaylist() || playlist->isTemporary()) {
        error = tr("Recovery marker no longer identifies a regular persistent playlist"); return false;
    }
    if(m_playlists->playlistById(playlist->id())) {
        if(diskCount != 1 || std::ranges::none_of(stored, [&](const auto& row) {
               return row.marker == record.target && row.dbId == playlist->dbId();
           })) {
            error = tr("Loaded playlist does not match its durable recovery identity");
            return false;
        }
        m_playlists->setPlaylistLocked(playlist->id(), false);
        return save(playlist, record.target, error);
    }
    // Removed objects are retained for undo, but their database rows are deleted.
    // Restore the public extra property too so a later host restore cannot bring
    // back our temporary lock. Do not recreate a deleted row or match a reused ID.
    if(diskCount) { error = tr("Removed playlist marker still exists in storage"); return false; }
    playlist->removeExtraProperty(QStringLiteral("core/locked"));
    return syncDatabase(error);
}

void PlaylistGuard::inhibitEditor(QAbstractItemView* view)
{
    if(!m_editors.contains(view) || !m_editors.at(view).view) {
        m_editors[view] = {view, view->editTriggers()};
    }
    else if(view->editTriggers() != QAbstractItemView::NoEditTriggers) {
        m_editors[view].triggers = view->editTriggers();
    }
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
}

bool PlaylistGuard::filter(QWidget* widget, WidgetInputFilter::Surface surface, QEvent* event)
{
    using Surface = WidgetInputFilter::Surface;
    if(surface == Surface::Unknown) return false;
    // A menu opened before locking can still contain unregistered commands.
    if(surface != Surface::Playlist && qobject_cast<QMenu*>(widget)) {
        switch(event->type()) {
            case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: case QEvent::MouseButtonDblClick:
            case QEvent::KeyPress: case QEvent::KeyRelease: case QEvent::ShortcutOverride:
                if(auto* key = dynamic_cast<QKeyEvent*>(event); key && key->key() == Qt::Key_Escape) return false;
                event->accept(); return true;
            default: break;
        }
    }
    if(surface == Surface::Manager) {
        for(auto* parent = widget; parent; parent = parent->parentWidget()) {
            if(auto* view = qobject_cast<QAbstractItemView*>(parent)) { inhibitEditor(view); break; }
        }
    }
    switch(event->type()) {
        case QEvent::DragEnter: case QEvent::DragMove: case QEvent::Drop:
            event->ignore(); return true;
        // Switcher menus contain unregistered lock/delete/restore actions. Keep
        // playlist-content menus (copy/search etc.) available via ActionGuard.
        case QEvent::ContextMenu:
            return surface != Surface::Playlist;
        case QEvent::MouseButtonDblClick:
            return surface != Surface::Playlist; // tab rename/create and tree edit
        case QEvent::MouseMove:
            if(surface == Surface::Tabs || surface == Surface::Organiser) {
                return static_cast<QMouseEvent*>(event)->buttons() != Qt::NoButton;
            }
            break;
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease:
            // Unsorted autoplaylists permit direct header sorting. Regular
            // content locks already veto it, but autoplaylists need this route.
            if(surface == Surface::Playlist) {
                for(auto* parent = widget; parent; parent = parent->parentWidget()) {
                    if(qobject_cast<QHeaderView*>(parent)) return true;
                    if(parent->inherits("Fooyin::PlaylistWidget")) break;
                }
            }
            if(surface == Surface::Tabs) {
                if(static_cast<QMouseEvent*>(event)->button() == Qt::MiddleButton) return true;
                // Tab close and optional corner add/clear buttons are direct
                // mutation routes. Qt's named scroll arrows remain available.
                if(qobject_cast<QAbstractButton*>(widget)
                   && widget->objectName() != QStringLiteral("ScrollLeftButton")
                   && widget->objectName() != QStringLiteral("ScrollRightButton")) return true;
            }
            break;
        case QEvent::KeyPress: case QEvent::KeyRelease: case QEvent::ShortcutOverride: {
            const auto* key = static_cast<QKeyEvent*>(event);
            if(surface != Surface::Playlist && (key->key() == Qt::Key_F2 || key->key() == Qt::Key_Delete)) {
                event->accept(); return true;
            }
            if(surface == Surface::Tabs && qobject_cast<QAbstractButton*>(widget)
               && (key->key() == Qt::Key_Space || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
                event->accept(); return true;
            }
            break;
        }
        default: break;
    }
    return false;
}
} // namespace Fooyin::SessionLock

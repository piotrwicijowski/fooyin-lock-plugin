// SPDX-License-Identifier: GPL-3.0-or-later
#include "browsingguard.h"
#include "widgetinputfilter.h"

#include <core/coresettings.h>
#include <gui/fywidget.h>
#include <gui/guipaths.h>
#include <gui/layout/layoutprovider.h>
#include <utils/settings/settingsmanager.h>

#include <QApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSignalBlocker>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Fooyin::SessionLock {
namespace {
const QString CursorKey = QStringLiteral("Playlist/PlaybackFollowsCursor");
const QString QuickSearchKey = QStringLiteral("Searching/QuickSearchState");
const QString QuickSearchMode = QStringLiteral("Searching/QuickSearchState/SearchMode");
constexpr int FilterSearchMode = 2; // Audited SearchWidget::SearchMode::PlaylistFilter.

bool searchWidget(const FyWidget* widget) { return widget->inherits("Fooyin::SearchWidget"); }

bool supported(const FyWidget* widget)
{
    return widget->inherits("Fooyin::LibraryTreeWidget") || widget->inherits("Fooyin::Filters::FilterWidget")
        || searchWidget(widget);
}

QString identity(const FyWidget* widget)
{
    return widget->layoutName() + u'/' + widget->id().name();
}

bool syncFile(const QString& path, QString& error)
{
#ifdef Q_OS_UNIX
    // Flush the file and parent directories before acknowledging recovery. This
    // also covers the atomic rename used by QSettings/QSaveFile.
    auto current = path;
    do {
        const int fd = ::open(QFile::encodeName(current).constData(), O_RDONLY | O_CLOEXEC);
        if(fd < 0) break;
        const bool ok = ::fsync(fd) == 0;
        ::close(fd);
        if(!ok) break;
        if(current == QStringLiteral("/")) return true;
        current = QFileInfo{current}.absolutePath();
    } while(true);
#endif
    error = QStringLiteral("Cannot durably sync browsing configuration: %1").arg(path);
    return false;
}

// Only exact persisted widget identities match. Names, group names, playlist
// names and positions in a layout are deliberately not recovery identities.
QJsonValue restored(QJsonValue value, const RecoveryRecord& record, bool& valid)
{
    if(value.isArray()) {
        auto array = value.toArray();
        for(auto it = array.begin(); it != array.end(); ++it) *it = restored(*it, record, valid);
        return array;
    }
    if(!value.isObject()) return value;
    auto object = value.toObject();
    for(auto it = object.begin(); it != object.end(); ++it) {
        auto child = it.value().toObject();
        if((it.key() == QStringLiteral("LibraryTree") || it.key() == QStringLiteral("LibraryFilter")
            || it.key() == QStringLiteral("SearchBar"))
           && record.target == it.key() + u'/' + child.value(QStringLiteral("ID")).toString()) {
            const auto key = it.key() == QStringLiteral("SearchBar") ? QStringLiteral("SearchMode")
                                                                    : QStringLiteral("PlaylistEnabled");
            if(child.contains(key)) {
                const auto original = QJsonValue::fromVariant(record.original);
                const auto current = child.value(key);
                if(current == QJsonValue::fromVariant(record.temporary)) child[key] = original;
                else if(current != original) {
                    // Another saved layout with this ID can legitimately have
                    // different configuration. It contains no owned temporary
                    // value: preserve it rather than copying a live snapshot.
                    valid = valid && (original.isBool() ? current.isBool()
                        : (current.isDouble() && current.toInt(-1) >= 0 && current.toInt(-1) <= 3
                           && current.toDouble() == current.toInt(-1)));
                }
            }
            it.value() = child;
        }
        else it.value() = restored(it.value(), record, valid);
    }
    return object;
}
} // namespace

BrowsingGuard::BrowsingGuard(SettingsManager* settings, LayoutProvider* layouts, QString settingsPath, QString layoutsPath)
    : m_settings{settings}, m_layouts{layouts}
    , m_settingsPath{settingsPath.isEmpty() ? FySettings{}.fileName() : std::move(settingsPath)}
    , m_layoutsPath{layoutsPath.isEmpty() ? Gui::layoutsPath() : std::move(layoutsPath)}
{ }

BrowsingGuard::~BrowsingGuard() { QString error; leave(error); }
QString BrowsingGuard::id() const { return QStringLiteral("browsing"); }

void BrowsingGuard::setWidgetValue(FyWidget* widget, const QVariant& value)
{
    if(searchWidget(widget)) {
        widget->loadLayoutData({{QStringLiteral("SearchMode"), value.toInt()}});
        return;
    }
    QJsonObject data;
    widget->saveLayoutData(data);
    data[QStringLiteral("PlaylistEnabled")] = value.toBool();
    // Do not restore grouping, model/header state or embedded search state just
    // to change configuration. In particular, never replay a saved selection.
    for(const auto* key : {"Grouping", "State", "SearchBar"}) {
        data.remove(QString::fromLatin1(key));
    }
    if(widget->inherits("Fooyin::Filters::FilterWidget")) {
        for(const auto* key : {"Columns", "Group", "Index", "Display", "Captions", "Artwork", "Source", "ShowSummary",
                               "ShowHeader", "ShowScrollbar", "AlternatingRows"}) data.remove(QString::fromLatin1(key));
    }
    // Both audited implementations synchronously apply config before publishing
    // configChanged/filterUpdated. Suppress those publications for this overlay
    // so changing the guard cannot trigger recomputation or a deferred write.
    const QSignalBlocker blocker{widget};
    widget->loadLayoutData(data);
}

bool BrowsingGuard::persistCursor(bool value, QString& error)
{
    if(!m_settings || !m_settings->contains(CursorKey) || m_settings->value(CursorKey).typeId() != QMetaType::Bool) {
        error = QStringLiteral("Playback-follows-cursor setting is unavailable");
        return false;
    }
    m_settings->set(CursorKey, value);
    m_settings->fileSet(CursorKey, value); // Also persist an unchanged factory default.
    QSettings stored{m_settingsPath, QSettings::IniFormat};
    stored.sync();
    if(m_settings->value(CursorKey) != QVariant{value} || !stored.contains(CursorKey)
       || stored.value(CursorKey).toBool() != value || stored.status() != QSettings::NoError) {
        error = QStringLiteral("Cannot verify playback-follows-cursor persistence");
        return false;
    }
    return syncFile(m_settingsPath, error);
}

bool BrowsingGuard::enter(RecoveryJournal& journal, QString& error)
{
    if(m_journal) return true;
    if(!qApp || !m_settings || m_settings->value(CursorKey).typeId() != QMetaType::Bool) {
        error = QStringLiteral("Browsing settings/application are unavailable");
        return false;
    }
    const auto original = m_settings->value(CursorKey);
    if(!journal.remember({id(), QStringLiteral("setting"), CursorKey, original, false}, error)) return false;
    m_journal = &journal;
    m_settings->subscribe(CursorKey, this, [this]() {
        if(!m_journal || m_updating) return;
        const QScopedValueRollback updating{m_updating, true};
        QString detail;
        if(!persistCursor(false, detail)) Q_EMIT protectionFailed(detail);
    });
    if(!persistCursor(false, error)) return false;
    if(!protectQuickSearch(error)) return false;
    // Do not clear PlayerController's generic scheduled-track slot. A request
    // submitted before entry remains part of the host's existing playback plan.
    qApp->installEventFilter(this);
    const QScopedValueRollback entryScan{m_entryScan, true};
    for(auto* candidate : QApplication::allWidgets()) {
        if(auto* widget = qobject_cast<FyWidget*>(candidate); widget && supported(widget)) {
            if(!protect(widget, error)) return false;
        }
    }
    return true;
}

bool BrowsingGuard::protect(FyWidget* widget, QString& error)
{
    if(searchWidget(widget)) {
        // The library tree's embedded bar has a direct search target and never
        // enters the generated-playlist branch, including forced submissions.
        if(auto* parent = widget->findParent(); parent && parent->inherits("Fooyin::LibraryTreeWidget")) return true;
    }
    QJsonObject data;
    widget->saveLayoutData(data);
    const auto target = identity(widget);
    const QVariant current = searchWidget(widget) ? QVariant{data.value(QStringLiteral("SearchMode")).toInt()}
                                                  : QVariant{data.value(QStringLiteral("PlaylistEnabled")).toBool()};
    auto original = current;
    const QVariant temporary = searchWidget(widget) ? QVariant{FilterSearchMode} : QVariant{false};
    const auto& records = m_journal->records();
    if(std::ranges::none_of(records, [&](const auto& record) {
           return record.guard == id() && record.kind == QStringLiteral("widget-setting") && record.target == target;
       })) {
        if(searchWidget(widget) && !widget->parentWidget() && !m_entryScan) {
            for(const auto& record : records) {
                if(record.target == QuickSearchMode) original = record.original;
            }
        }
        if(!m_journal->remember({id(), QStringLiteral("widget-setting"), target,
                                 original, temporary}, error)) return false;
    }
    if(std::ranges::none_of(m_widgets, [widget](const auto& known) { return known.widget == widget; })) {
        m_widgets.push_back({widget, widget->hasFeature(FyWidget::PersistId)});
        // Search bars don't normally persist IDs. Give temporary mode overrides
        // an exact recoverable identity if the host saves a layout while locked.
        if(searchWidget(widget)) widget->setFeature(FyWidget::PersistId);
        // Public Qt metaobject signal; concrete widget headers are private.
        if(!searchWidget(widget)) {
            connect(widget, SIGNAL(configChanged()), this, SLOT(enforceWidgets()),
                    static_cast<Qt::ConnectionType>(Qt::DirectConnection | Qt::UniqueConnection));
        }
    }
    if(current != temporary) setWidgetValue(widget, temporary);
    if(searchWidget(widget)) {
        if(auto* editor = widget->findChild<QLineEdit*>(QString{}, Qt::FindDirectChildrenOnly)) {
            // Unconnected search has no in-widget results to publish. Keep text
            // editing usable but prevent textChanged from arming a deferred
            // result-playlist write which could fire after unlock. Connected and
            // embedded searches retain their normal live text notifications.
            const bool connected = !data.value(QStringLiteral("Widgets")).toString().isEmpty()
                && editor->toolTip().isEmpty(); // Audited unconnected-state tooltip, including stale connections.
            const auto known = std::ranges::find_if(m_searchEditors, [editor](const auto& entry) {
                return entry.editor == editor;
            });
            if(!connected) {
                if(known == m_searchEditors.end()) m_searchEditors.push_back({editor, editor->signalsBlocked()});
                editor->blockSignals(true);
            }
            else if(known != m_searchEditors.end()) {
                editor->blockSignals(known->blocked);
                m_searchEditors.erase(known);
            }
        }
    }
    return true;
}

bool BrowsingGuard::persistQuickSearchMode(int mode, QString& error)
{
    FyStateSettings state;
    auto data = state.value(QuickSearchKey).toJsonObject();
    data[QStringLiteral("SearchMode")] = mode;
    state.setValue(QuickSearchKey, data);
    state.sync();
    if(state.status() != QSettings::NoError || state.value(QuickSearchKey).toJsonObject() != data) {
        error = QStringLiteral("Cannot persist quick-search mode"); return false;
    }
    return syncFile(state.fileName(), error);
}

bool BrowsingGuard::protectQuickSearch(QString& error)
{
    const auto data = FyStateSettings{}.value(QuickSearchKey).toJsonObject();
    if(data.isEmpty()) return true;
    const auto value = data.value(QStringLiteral("SearchMode"));
    const int mode = value.toInt();
    if(data.contains(QStringLiteral("SearchMode"))
       && (!value.isDouble() || mode < 0 || mode > 3 || value.toDouble() != mode)) {
        error = QStringLiteral("Unknown saved quick-search mode"); return false;
    }
    const auto& records = m_journal->records();
    if(std::ranges::none_of(records, [&](const auto& record) { return record.target == QuickSearchMode; })) {
        if(!m_journal->remember({id(), QStringLiteral("quick-search-mode"), QuickSearchMode, mode, FilterSearchMode},
                               error)) return false;
    }
    return persistQuickSearchMode(FilterSearchMode, error);
}

void BrowsingGuard::enforceWidgets()
{
    if(!m_journal || m_updating) return;
    const QScopedValueRollback updating{m_updating, true};
    QString error;
    for(auto* candidate : QApplication::allWidgets()) {
        if(auto* widget = qobject_cast<FyWidget*>(candidate); widget && supported(widget) && !protect(widget, error)) {
            Q_EMIT protectionFailed(error);
            return;
        }
    }
}

bool BrowsingGuard::eventFilter(QObject* receiver, QEvent* event)
{
    if(m_updating || !m_journal) return false;
    if(event->type() == QEvent::Show) {
        if(auto* widget = qobject_cast<FyWidget*>(receiver); widget && searchWidget(widget) && !widget->parentWidget()) {
            // Quick Search can search immediately after loading state in showEvent.
            QString error;
            if(!protectQuickSearch(error)) Q_EMIT protectionFailed(error);
        }
    }
    switch(event->type()) {
        // Discover completed late/layout-restored widgets before input or queued
        // model work can publish selection. Never inspect partially built widgets
        // on ChildAdded/ChildPolished/construction-time property changes.
        case QEvent::PolishRequest: case QEvent::Show: case QEvent::MetaCall: case QEvent::Timer:
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: case QEvent::MouseButtonDblClick:
        case QEvent::KeyPress: case QEvent::ShortcutOverride: case QEvent::ContextMenu:
            enforceWidgets(); break;
        case QEvent::Close:
            // Standalone LibraryTree windows save config without a persisted ID
            // in their close handler. Remove the overlay before that save.
            if(auto* widget = qobject_cast<FyWidget*>(receiver); widget && m_journal && supported(widget)) {
                for(const auto& record : m_journal->records()) {
                    if(record.kind == QStringLiteral("widget-setting") && record.target == identity(widget)) {
                        const QScopedValueRollback updating{m_updating, true};
                        QPointer<FyWidget> alive{widget};
                        setWidgetValue(widget, record.original);
                        QCoreApplication::sendEvent(widget, event);
                        if(alive) setWidgetValue(alive, record.temporary);
                        return true;
                    }
                }
            }
            break;
        default: break;
    }
    auto* widget = qobject_cast<QWidget*>(receiver);
    if(widget && WidgetInputFilter::isSearchSubmit(widget, event)
       && (static_cast<QKeyEvent*>(event)->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier))) {
        // Modifier+Enter forces library/playlist/all-playlist result generation,
        // bypassing SearchMode. Plain Enter and connected/embedded search remain.
        event->accept(); return true;
    }
    return false; // Selection, searching, expansion and text editing still run.
}

bool BrowsingGuard::leave(QString&)
{
    if(qApp) qApp->removeEventFilter(this);
    if(m_settings) m_settings->unsubscribe(CursorKey, this);
    for(const auto& entry : m_searchEditors) if(entry.editor) entry.editor->blockSignals(entry.blocked);
    m_searchEditors.clear();
    for(const auto& observed : m_widgets) {
        if(observed.widget) {
            disconnect(observed.widget, nullptr, this, nullptr);
            if(searchWidget(observed.widget)) observed.widget->setFeature(FyWidget::PersistId, observed.persistId);
        }
    }
    m_widgets.clear();
    m_journal = nullptr;
    return true;
}

bool BrowsingGuard::restoreLayouts(const RecoveryRecord& record, QString& error)
{
    // A host layout save during the session can persist the temporary flag.
    // Repair all managed files by exact ID, including currently inactive layouts,
    // before dropping the original. QSaveFile avoids truncating a layout on error.
    const QFileInfo directory{m_layoutsPath};
    if(directory.exists() && (!directory.isDir() || !directory.isReadable())) {
        error = QStringLiteral("Cannot inspect saved layouts: %1").arg(m_layoutsPath); return false;
    }
    QDirIterator files{m_layoutsPath, {QStringLiteral("*.fyl")}, QDir::Files, QDirIterator::Subdirectories};
    while(files.hasNext()) {
        QFile file{files.next()};
        if(!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
        file.close();
        if(parse.error != QJsonParseError::NoError || !document.isObject()) {
            error = QStringLiteral("Cannot verify saved layout: %1").arg(file.fileName()); return false;
        }
        bool valid{true};
        const auto output = restored(document.object(), record, valid).toObject();
        if(!valid) { error = QStringLiteral("Conflicting saved widget setting: %1").arg(record.target); return false; }
        if(output != document.object()) {
            QSaveFile save{file.fileName()};
            const auto bytes = QJsonDocument{output}.toJson();
            if(!save.open(QIODevice::WriteOnly) || save.write(bytes) != bytes.size() || !save.commit()) {
                error = save.errorString(); return false;
            }
        }
        // Also sync an already-restored file: an interrupted previous recovery
        // may have renamed it but not yet made the rename durable.
        if(!syncFile(file.fileName(), error)) return false;
    }
    if(m_layouts) {
        for(const auto& layout : m_layouts->layouts()) {
            bool valid{true};
            const auto output = restored(layout.json(), record, valid).toObject();
            if(!valid) { error = QStringLiteral("Conflicting loaded layout setting: %1").arg(record.target); return false; }
            if(output != layout.json()) m_layouts->updateLayout(FyLayout{layout.name(), output});
        }
    }
    return true;
}

bool BrowsingGuard::recover(const RecoveryRecord& record, QString& error)
{
    const bool boolean = record.original.typeId() == QMetaType::Bool
        && record.temporary.typeId() == QMetaType::Bool && !record.temporary.toBool();
    const bool searchMode = record.original.typeId() == QMetaType::Int && record.temporary.typeId() == QMetaType::Int
        && record.original.toInt() >= 0 && record.original.toInt() <= 3 && record.temporary.toInt() == FilterSearchMode;
    if(record.guard != id() || (!boolean && !searchMode)) {
        error = QStringLiteral("Unsupported browsing recovery record"); return false;
    }
    if(record.kind == QStringLiteral("setting") && record.target == CursorKey && boolean) {
        const auto current = m_settings ? m_settings->value(CursorKey) : QVariant{};
        return persistCursor(current == record.temporary ? record.original.toBool() : current.toBool(), error);
    }
    if(record.kind == QStringLiteral("quick-search-mode") && record.target == QuickSearchMode && searchMode) {
        FyStateSettings state;
        const auto data = state.value(QuickSearchKey).toJsonObject();
        if(!data.contains(QStringLiteral("SearchMode"))) {
            return !QFileInfo::exists(state.fileName()) || syncFile(state.fileName(), error);
        }
        const auto mode = data.value(QStringLiteral("SearchMode"));
        if(!mode.isDouble() || mode.toInt(-1) < 0 || mode.toInt(-1) > 3 || mode.toDouble() != mode.toInt(-1)) {
            error = QStringLiteral("Unknown saved quick-search mode"); return false;
        }
        return persistQuickSearchMode(mode.toInt() == FilterSearchMode ? record.original.toInt() : mode.toInt(), error);
    }
    const auto slash = record.target.indexOf(u'/');
    const auto type = record.target.left(slash);
    if(record.kind != QStringLiteral("widget-setting") || slash < 1 || record.target.mid(slash + 1).isEmpty()
       || !((boolean && (type == QStringLiteral("LibraryTree") || type == QStringLiteral("LibraryFilter")))
            || (searchMode && type == QStringLiteral("SearchBar")))) {
        error = QStringLiteral("Unsupported browsing recovery target"); return false;
    }
    if(!restoreLayouts(record, error)) return false;
    for(auto* candidate : QApplication::allWidgets()) {
        if(auto* widget = qobject_cast<FyWidget*>(candidate); widget && supported(widget)
           && identity(widget) == record.target) {
            QJsonObject data; widget->saveLayoutData(data);
            const auto key = searchWidget(widget) ? QStringLiteral("SearchMode") : QStringLiteral("PlaylistEnabled");
            if(data.value(key) == QJsonValue::fromVariant(record.temporary)) setWidgetValue(widget, record.original);
        }
    }
    return true;
}
} // namespace Fooyin::SessionLock

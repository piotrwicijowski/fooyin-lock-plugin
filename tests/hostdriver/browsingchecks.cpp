// SPDX-License-Identifier: GPL-3.0-or-later
#include "browsingchecks.h"

#include <core/library/musiclibrary.h>
#include <core/coresettings.h>
#include <core/player/playercontroller.h>
#include <core/playlist/playlisthandler.h>
#include <core/plugins/coreplugincontext.h>
#include <gui/fywidget.h>
#include <gui/guisettings.h>
#include <gui/layout/editablelayout.h>
#include <gui/layout/fylayout.h>
#include <gui/playlist/currentplaylistcontroller.h>
#include <gui/plugins/guiplugincontext.h>
#include <gui/widgetprovider.h>
#include <utils/database/dbconnectionprovider.h>
#include <utils/settings/settingsmanager.h>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSqlQuery>
#include <QThread>

#include <functional>
#include <memory>

using namespace Fooyin;
namespace {
const QString CursorKey = QStringLiteral("Playlist/PlaybackFollowsCursor");
bool waitFor(const std::function<bool()>& predicate)
{
    QElapsedTimer timer; timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if(predicate()) return true;
        QThread::msleep(1);
    } while(timer.elapsed() < 3000);
    return false;
}
void settle()
{
    QElapsedTimer timer; timer.start();
    while(timer.elapsed() < 80) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
}
bool playlistEnabled(FyWidget* widget)
{
    QJsonObject data; widget->saveLayoutData(data);
    return data.value(QStringLiteral("PlaylistEnabled")).toBool();
}
void select(QAbstractItemView* view, int row)
{
    view->selectionModel()->setCurrentIndex(view->model()->index(row, 0),
                                           QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

int searchMode(FyWidget* root)
{
    QJsonObject data; root->saveLayoutData(data);
    return data.value(QStringLiteral("SearchMode")).toInt();
}

bool checkConnectedSearch(const CorePluginContext& core, const GuiPluginContext& gui, QAction* toggle,
                          const TrackList& tracks)
{
    const auto previous = gui.editableLayout->saveCurrentToLayout(QStringLiteral("SessionLock previous"), false);
    const QString sourceName = QStringLiteral("SessionLock connected source");
    auto* source = core.playlistHandler->createPlaylist(sourceName, tracks);
    if(!previous.isValid() || !source) return false;
    source->changeCurrentIndex(0);
    core.playlistHandler->changeActivePlaylist(source);
    const auto count = core.playlistHandler->playlists().size();
    const QJsonArray children{
        QJsonObject{{QStringLiteral("SearchBar"), QJsonObject{{QStringLiteral("ID"), "lock-linked-search"},
            {QStringLiteral("Widgets"), "lock-linked-tree"}, {QStringLiteral("AutoSearch"), true},
            {QStringLiteral("SearchMode"), 3}}}},
        QJsonObject{{QStringLiteral("LibraryTree"), QJsonObject{{QStringLiteral("ID"), "lock-linked-tree"},
            {QStringLiteral("PlaylistEnabled"), true}, {QStringLiteral("PlaylistName"), sourceName},
            {QStringLiteral("AutoSwitch"), true}}}}};
    const FyLayout linked{QStringLiteral("SessionLock linked"),
        {{QStringLiteral("Name"), "SessionLock linked"}, {QStringLiteral("Widgets"),
          QJsonArray{QJsonObject{{QStringLiteral("SplitterVertical"), QJsonObject{{QStringLiteral("Widgets"), children}}}}}}}};
    toggle->trigger();
    if(!toggle->isChecked()) return false;
    gui.editableLayout->changeLayout(linked);
    auto* search = gui.editableLayout->findWidget(Id{"lock-linked-search"});
    auto* tree = gui.editableLayout->findWidget(Id{"lock-linked-tree"});
    auto* editor = search ? search->findChild<QLineEdit*>() : nullptr;
    auto* view = tree ? tree->findChild<QAbstractItemView*>() : nullptr;
    if(!editor || !view || !waitFor([&]() { return view->model()->rowCount() > 1; })) {
        qCritical("Connected-search layout did not populate"); return false;
    }
    search->hide(); search->show(); // Refresh the host's connection-state view after layout registration.
    settle();
    if(editor->signalsBlocked() || playlistEnabled(tree) || searchMode(search) != 2) {
        qCritical("Connected-search configuration/notifications were not protected correctly"); return false;
    }
    const int before = view->model()->rowCount();
    editor->setText(QStringLiteral("__session_lock_missing__"));
    if(!waitFor([&]() { return view->model()->rowCount() < before; })) {
        qCritical("Connected-search query did not reach the library tree"); return false;
    }
    editor->clear();
    if(!waitFor([&]() { return view->model()->rowCount() > 1; })) return false;
    if(source->name() != sourceName || source->currentTrackIndex() != 0
       || core.playlistHandler->playlists().size() != count || core.playlistHandler->activePlaylist() != source) return false;
    toggle->trigger();
    if(toggle->isChecked() || !playlistEnabled(tree) || searchMode(search) != 3) return false;
    gui.editableLayout->changeLayout(previous);
    core.playlistHandler->removePlaylist(source->id());
    return true;
}

bool checkSearches(const CorePluginContext& core, const GuiPluginContext& gui, QAction* toggle, const TrackList& tracks)
{
    auto* input = core.playlistHandler->createPlaylist(QStringLiteral("SessionLock search input"), tracks);
    if(!input) return false;
    for(int mode : {0, 1, 3}) {
        const QString name = QStringLiteral("SessionLock search %1").arg(mode);
        auto* source = core.playlistHandler->createPlaylist(name, tracks);
        if(!source) return false;
        source->changeCurrentIndex(0);
        core.playlistHandler->changeActivePlaylist(source);
        gui.playlistSelection->changeCurrentPlaylist(source->id());
        core.settingsManager->set<Settings::Gui::SearchPlaylistName>(name);
        core.settingsManager->set<Settings::Gui::SearchPlaylistAppendSearch>(true);
        std::unique_ptr<FyWidget> root{gui.widgetProvider->createWidget(QStringLiteral("SearchBar"))};
        if(!root) return false;
        root->loadLayoutData({{QStringLiteral("SearchMode"), mode}, {QStringLiteral("AutoSearch"), true}});
        root->show();
        auto* editor = root->findChild<QLineEdit*>();
        if(!editor) return false;
        const auto count = core.playlistHandler->playlists().size();
        toggle->trigger();
        if(!toggle->isChecked() || searchMode(root.get()) != 2) return false;
        editor->setText(QStringLiteral("Lock")); settle(); settle();
        for(const auto modifiers : {Qt::NoModifier, Qt::ControlModifier, Qt::AltModifier, Qt::ShiftModifier}) {
            QKeyEvent enter{QEvent::KeyPress, Qt::Key_Return, modifiers};
            QCoreApplication::sendEvent(editor, &enter);
        }
        settle();
        if(source->name() != name || source->currentTrackIndex() != 0
           || core.playlistHandler->playlists().size() != count) return false;
        editor->setText(QStringLiteral("Beta")); // Unlock before an autosearch debounce could run.
        toggle->trigger();
        if(toggle->isChecked() || searchMode(root.get()) != mode || root->hasFeature(FyWidget::PersistId)) return false;
        settle();
        if(source->name() != name) return false; // No deferred autosearch on unlock.
        editor->setText(QStringLiteral("Alpha"));
        if(!waitFor([&]() { return source->name() != name; })) {
            qCritical() << "Unlocked search mode did not resume:" << mode; return false;
        }
        root.reset();
        const auto lists = core.playlistHandler->playlists();
        for(auto* list : lists) if(list->name().startsWith(name)) core.playlistHandler->removePlaylist(list->id());
    }

    // Quick Search's show-all path loads persistent state and submits from
    // showEvent, before the user has typed anything. Exercise a late instance,
    // normal close/save and reopening while still locked.
    FyStateSettings state;
    state.setValue(QStringLiteral("Searching/QuickSearchState"),
                   QJsonObject{{QStringLiteral("SearchMode"), 3}, {QStringLiteral("ShowAll"), true},
                               {QStringLiteral("AutoSearch"), true}});
    state.sync();
    const auto count = core.playlistHandler->playlists().size();
    toggle->trigger();
    if(!toggle->isChecked()) return false;
    std::unique_ptr<FyWidget> quick{gui.widgetProvider->createWidget(QStringLiteral("SearchBar"))};
    if(!quick) return false;
    quick->setParent(nullptr);
    quick->show();
    settle(); settle();
    if(searchMode(quick.get()) != 2 || core.playlistHandler->playlists().size() != count) return false;
    quick->close();
    if(state.value(QStringLiteral("Searching/QuickSearchState")).toJsonObject()
           .value(QStringLiteral("SearchMode")).toInt() != 3) return false;
    quick->show(); settle();
    if(searchMode(quick.get()) != 2) return false;
    toggle->trigger();
    if(toggle->isChecked() || searchMode(quick.get()) != 3) return false;
    settle();
    if(core.playlistHandler->playlists().size() != count) return false;
    if(!checkConnectedSearch(core, gui, toggle, tracks)) return false;
    core.playlistHandler->removePlaylist(input->id());
    qInfo("Session Lock smoke: autosearch, forced search submission, quick-search show-all, and search-mode restoration verified");
    return true;
}
}

bool checkBrowsing(const CorePluginContext& core, const GuiPluginContext& gui, QAction* toggle)
{
    // Real file-backed library records exercise both actual host model pipelines.
    // No audio decoder or private model headers are needed.
    QSqlQuery query{DbConnectionProvider{core.dbPool}.db()};
    for(const auto& sql : {
            QStringLiteral("INSERT OR IGNORE INTO Tracks (TrackID, FilePath, Title, Artists, Album, LibraryID, TrackHash) "
                           "VALUES (10001, '/tmp/lock-browse-a.flac', 'Lock Alpha', 'Artist A', 'Album A', 42, 'lock-a')"),
            QStringLiteral("INSERT OR IGNORE INTO Tracks (TrackID, FilePath, Title, Artists, Album, LibraryID, TrackHash) "
                           "VALUES (10002, '/tmp/lock-browse-b.flac', 'Lock Beta', 'Artist B', 'Album B', 42, 'lock-b')")}) {
        if(!query.exec(sql)) return false;
    }
    core.library->loadAllTracks();
    if(!waitFor([&]() { return core.library->visibleLibraryTracks().size() == 2; })) {
        qCritical("Browsing fixture library did not load"); return false;
    }
    const auto tracks = core.library->visibleLibraryTracks();
    std::unique_ptr<FyWidget> tabs{gui.widgetProvider->createWidget(QStringLiteral("PlaylistTabs"))};
    if(!tabs) return false;
    for(bool cursor : {false, true}) {
        for(bool keepAlive : {false, true}) {
            for(bool collision : {false, true}) {
                const auto suffix = QStringLiteral("%1-%2-%3").arg(cursor).arg(keepAlive).arg(collision);
                const QString sourceName = QStringLiteral("SessionLock source ") + suffix;
                const QString resultName = collision ? sourceName : QStringLiteral("SessionLock results ") + suffix;
                auto* source = core.playlistHandler->createPlaylist(sourceName, tracks);
                auto* viewed = core.playlistHandler->createPlaylist(QStringLiteral("SessionLock viewed ") + suffix, tracks);
                if(!source || !viewed) return false;
                source->changeCurrentIndex(0);
                core.playlistHandler->changeActivePlaylist(source);
                gui.playlistSelection->changeCurrentPlaylist(source->id());
                core.settingsManager->set(CursorKey, cursor);
                core.playerController->scheduleNextTrack(*source->playlistTrack(1));
                const auto upcoming = core.playerController->upcomingPlaylistTrack();
                std::vector<std::unique_ptr<FyWidget>> roots;
                for(const auto& key : {QStringLiteral("LibraryTree"), QStringLiteral("LibraryFilter")}) {
                    std::unique_ptr<FyWidget> root{gui.widgetProvider->createWidget(key)};
                    if(!root) return false;
                    QJsonObject data; root->saveLayoutData(data);
                    data[QStringLiteral("PlaylistEnabled")] = true;
                    data[QStringLiteral("PlaylistName")] = resultName;
                    data[QStringLiteral("KeepAlive")] = keepAlive;
                    data[QStringLiteral("AutoSwitch")] = true;
                    if(key == QStringLiteral("LibraryTree")) data[QStringLiteral("ShowControls")] = true;
                    root->loadLayoutData(data);
                    root->finalise();
                    auto* view = root->findChild<QAbstractItemView*>();
                    if(!view || !waitFor([&]() { return view->model()->rowCount() > 1; })) {
                        qCritical() << "Browsing model did not populate:" << key; return false;
                    }
                    roots.push_back(std::move(root));
                }
                const auto count = core.playlistHandler->playlists().size();
                toggle->trigger();
                if(!toggle->isChecked() || core.settingsManager->value(CursorKey).toBool()) return false;
                gui.playlistSelection->selectTracks({tracks[0]});
                gui.playlistSelection->changeCurrentPlaylist(viewed->id());
                for(auto& root : roots) {
                    auto* view = root->findChild<QAbstractItemView*>();
                    if(playlistEnabled(root.get())) return false;
                    select(view, 1);
                    if(!view->selectionModel()->hasSelection()) return false;
                    const int beforeSearch = view->model()->rowCount();
                    QLineEdit* embeddedEditor{nullptr};
                    for(auto* child : root->findChildren<FyWidget*>()) {
                        if(child->inherits("Fooyin::SearchWidget")) embeddedEditor = child->findChild<QLineEdit*>();
                    }
                    if(embeddedEditor) {
                        if(embeddedEditor->signalsBlocked()) return false;
                        embeddedEditor->setText(QStringLiteral("__session_lock_missing__"));
                    }
                    else root->searchEvent({QStringLiteral("__session_lock_missing__"), EmptySearchMode::ShowAll});
                    if(!waitFor([&]() { return view->model()->rowCount() < beforeSearch; })) return false;
                    if(embeddedEditor) embeddedEditor->clear();
                    else root->searchEvent({{}, EmptySearchMode::ShowAll});
                    if(!waitFor([&]() { return view->model()->rowCount() > 1; })) return false;
                    QJsonObject data; root->saveLayoutData(data);
                    data[QStringLiteral("PlaylistEnabled")] = true;
                    root->loadLayoutData(data);
                    if(playlistEnabled(root.get())) return false;
                    view->selectionModel()->clearSelection(); select(view, 0);
                }
                settle();
                if(core.playlistHandler->activePlaylist() != source || source->name() != sourceName
                   || source->tracks() != tracks || source->currentTrackIndex() != 0
                   || core.playlistHandler->playlists().size() != count
                   || gui.playlistSelection->currentPlaylist() != viewed
                   || core.playerController->upcomingPlaylistTrack() != upcoming) {
                    qCritical() << "Browsing changed the protected plan:" << suffix; return false;
                }
                toggle->trigger();
                if(toggle->isChecked() || core.settingsManager->value(CursorKey).toBool() != cursor) return false;
                settle(); // Unlock must not replay a suppressed selection.
                if(core.playlistHandler->playlists().size() != count || source->name() != sourceName) return false;
                for(auto& root : roots) if(!playlistEnabled(root.get())) return false;
                auto* tree = roots.front().get();
                tree->searchEvent({{}, EmptySearchMode::ShowAll}); settle();
                auto* view = tree->findChild<QAbstractItemView*>();
                view->selectionModel()->clearSelection(); select(view, 0);
                if(!waitFor([&]() {
                       auto* result = core.playlistHandler->playlistByName(resultName);
                       return result && result->currentTrackIndex() == -1
                           && gui.playlistSelection->currentPlaylist() == result;
                   })) return false;
                roots.clear();
                const auto lists = core.playlistHandler->playlists();
                for(auto* list : lists) {
                    if(list->name().startsWith(QStringLiteral("SessionLock source "))
                       || list->name().startsWith(QStringLiteral("SessionLock viewed "))
                       || list->name().startsWith(QStringLiteral("SessionLock results "))) {
                        core.playlistHandler->removePlaylist(list->id());
                    }
                }
            }
        }
    }
    if(!checkSearches(core, gui, toggle, tracks)) return false;
    core.playerController->scheduleNextTrack({}); // Dispose only the fixture's existing plan.
    core.settingsManager->set(CursorKey, true);
    qInfo("Session Lock smoke: cursor scheduling, generated-playlist collisions, search, switching, and restoration verified");
    return true;
}

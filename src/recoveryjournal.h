// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVariant>

#include <vector>

namespace Fooyin::SessionLock {
inline constexpr auto PlaylistIdentityProperty = "SessionLock.RecoveryIdentity";

// Playlist targets are persistent UUID markers, NEVER Playlist::id(), dbId(),
// names or indexes. The playlist guard owns persisting/looking up that marker.
struct RecoveryRecord
{
    QString guard;
    QString kind; // "playlist-lock", "setting", "widget-setting" or "quick-search-mode"
    QString target;
    QVariant original;
    QVariant temporary;

    bool operator==(const RecoveryRecord&) const = default;
};

class RecoveryJournal
{
public:
    explicit RecoveryJournal(QString path);

    bool load(QString& error);
    // Commit the original before performing ANY persistent temporary mutation.
    bool remember(const RecoveryRecord& record, QString& error);
    bool forget(const RecoveryRecord& record, QString& error);
    [[nodiscard]] const std::vector<RecoveryRecord>& records() const;

private:
    bool save(const std::vector<RecoveryRecord>& records, QString& error);

    QString m_path;
    std::vector<RecoveryRecord> m_records;
    bool m_loaded{false};
};
} // namespace Fooyin::SessionLock

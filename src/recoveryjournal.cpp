// SPDX-License-Identifier: GPL-3.0-or-later
#include "recoveryjournal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Fooyin::SessionLock {
namespace {
QJsonObject encode(const QVariant& value)
{
    QString type;
    QJsonValue data;
    switch(value.typeId()) {
        case QMetaType::Bool: type = QStringLiteral("bool"); data = value.toBool(); break;
        case QMetaType::Int: type = QStringLiteral("int"); data = value.toInt(); break;
        case QMetaType::QString: type = QStringLiteral("string"); data = value.toString(); break;
        case QMetaType::QStringList:
            type = QStringLiteral("string-list"); data = QJsonArray::fromStringList(value.toStringList()); break;
        default: return {};
    }
    return {{QStringLiteral("type"), type}, {QStringLiteral("value"), data}};
}

QVariant decode(const QJsonValue& value)
{
    const auto object = value.toObject();
    const auto type = object.value(QStringLiteral("type")).toString();
    const auto data = object.value(QStringLiteral("value"));
    if(type == QStringLiteral("bool") && data.isBool()) {
        return data.toBool();
    }
    if(type == QStringLiteral("int") && data.isDouble()) {
        const double number = data.toDouble();
        if(std::isfinite(number) && std::floor(number) == number
           && number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max()) {
            return static_cast<int>(number);
        }
    }
    if(type == QStringLiteral("string") && data.isString()) {
        return data.toString();
    }
    if(type == QStringLiteral("string-list") && data.isArray()) {
        QStringList result;
        for(const auto& entry : data.toArray()) {
            if(!entry.isString()) {
                return {};
            }
            result.append(entry.toString());
        }
        return result;
    }
    return {};
}

bool valid(const RecoveryRecord& record)
{
    if(record.guard.isEmpty() || record.target.isEmpty() || encode(record.original).isEmpty()
       || encode(record.temporary).isEmpty() || record.original.typeId() != record.temporary.typeId()) {
        return false;
    }
    if(record.kind == QStringLiteral("setting")) {
        return true;
    }
    if(record.kind == QStringLiteral("widget-setting")) {
        const auto slash = record.target.indexOf(u'/');
        if(slash < 1 || record.target.mid(slash + 1).isEmpty()) return false;
        const auto type = record.target.left(slash);
        if(type == QStringLiteral("LibraryTree") || type == QStringLiteral("LibraryFilter")) {
            return record.original.typeId() == QMetaType::Bool && !record.temporary.toBool();
        }
        return type == QStringLiteral("SearchBar") && record.original.typeId() == QMetaType::Int
            && record.original.toInt() >= 0 && record.original.toInt() <= 3 && record.temporary.toInt() == 2;
    }
    if(record.kind == QStringLiteral("quick-search-mode")) {
        return record.target == QStringLiteral("Searching/QuickSearchState/SearchMode")
            && record.original.typeId() == QMetaType::Int && record.original.toInt() >= 0
            && record.original.toInt() <= 3 && record.temporary.toInt() == 2;
    }
    const QUuid identity{record.target};
    return record.kind == QStringLiteral("playlist-lock") && !identity.isNull()
        && identity.toString(QUuid::WithoutBraces) == record.target
        && record.original.typeId() == QMetaType::Bool;
}

bool sameTarget(const RecoveryRecord& left, const RecoveryRecord& right)
{
    return left.kind == right.kind && left.target == right.target;
}
}

RecoveryJournal::RecoveryJournal(QString path)
    : m_path{std::move(path)}
{ }

bool RecoveryJournal::load(QString& error)
{
    m_loaded = false;
    QFile file{m_path};
    if(!file.exists()) {
        m_records.clear();
        m_loaded = true;
        return true;
    }
    if(!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot read recovery journal: %1").arg(file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto root = document.object();
    if(parseError.error != QJsonParseError::NoError || root.value(QStringLiteral("version")) != QJsonValue{1}
       || !root.value(QStringLiteral("records")).isArray()) {
        error = QStringLiteral("Incomplete or unsupported Session Lock recovery journal: %1").arg(m_path);
        return false;
    }
    std::vector<RecoveryRecord> records;
    for(const auto& value : root.value(QStringLiteral("records")).toArray()) {
        const auto object = value.toObject();
        RecoveryRecord record{object.value(QStringLiteral("guard")).toString(),
                              object.value(QStringLiteral("kind")).toString(),
                              object.value(QStringLiteral("target")).toString(),
                              decode(object.value(QStringLiteral("original"))),
                              decode(object.value(QStringLiteral("temporary")))};
        if(!valid(record) || std::ranges::any_of(records, [&](const auto& other) { return sameTarget(record, other); })) {
            error = QStringLiteral("Incomplete or conflicting Session Lock recovery record: %1").arg(m_path);
            return false;
        }
        records.push_back(std::move(record));
    }
    m_records = std::move(records);
    m_loaded = true;
    return true;
}

bool RecoveryJournal::remember(const RecoveryRecord& record, QString& error)
{
    if(!m_loaded || !valid(record)) {
        error = QStringLiteral("Recovery journal is not loaded or the original value/identity is invalid");
        return false;
    }
    for(const auto& existing : m_records) {
        if(sameTarget(existing, record)) {
            if(existing == record) {
                // Retry the durable write too: a preceding directory sync may have failed.
                return save(m_records, error);
            }
            error = QStringLiteral("A recovery original already exists for %1").arg(record.target);
            return false;
        }
    }
    auto records = m_records;
    records.push_back(record);
    // Retain intent in memory even if commit/sync fails; rollback is idempotent.
    m_records = records;
    return save(records, error);
}

bool RecoveryJournal::forget(const RecoveryRecord& record, QString& error)
{
    auto records = m_records;
    std::erase(records, record);
    if(!m_loaded || !save(records, error)) {
        return false;
    }
    m_records = std::move(records);
    return true;
}

const std::vector<RecoveryRecord>& RecoveryJournal::records() const
{
    return m_records;
}

bool RecoveryJournal::save(const std::vector<RecoveryRecord>& records, QString& error)
{
    const auto directory = QFileInfo{m_path}.absolutePath();
    if(!QDir{}.mkpath(directory)) {
        error = QStringLiteral("Cannot create recovery directory: %1").arg(directory);
        return false;
    }
    QJsonArray entries;
    for(const auto& record : records) {
        entries.append(QJsonObject{{QStringLiteral("guard"), record.guard}, {QStringLiteral("kind"), record.kind},
                                   {QStringLiteral("target"), record.target},
                                   {QStringLiteral("original"), encode(record.original)},
                                   {QStringLiteral("temporary"), encode(record.temporary)}});
    }
    const auto data = QJsonDocument{QJsonObject{{QStringLiteral("version"), 1},
                                               {QStringLiteral("records"), entries}}}.toJson();
    QSaveFile file{m_path};
    file.setDirectWriteFallback(false);
    bool success = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.flush();
#ifdef Q_OS_UNIX
    success = success && ::fsync(file.handle()) == 0;
#endif
    success = success && file.commit();
#ifdef Q_OS_UNIX
    // Sync ancestors too: mkpath may have introduced new directory entries.
    QDir syncDirectory{directory};
    while(success) {
        const int fd = ::open(QFile::encodeName(syncDirectory.absolutePath()).constData(), O_RDONLY | O_DIRECTORY);
        success = fd >= 0 && ::fsync(fd) == 0;
        if(fd >= 0) {
            ::close(fd);
        }
        if(syncDirectory.isRoot() || !syncDirectory.cdUp()) {
            break;
        }
    }
#endif
    if(!success) {
        error = QStringLiteral("Cannot durably commit recovery journal: %1 (%2)").arg(m_path, file.errorString());
    }
    return success;
}
} // namespace Fooyin::SessionLock

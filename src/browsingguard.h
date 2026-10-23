// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lockcoordinator.h"

#include <QPointer>

#include <vector>

class QLineEdit;

namespace Fooyin {
class FyWidget;
class LayoutProvider;
class SettingsManager;
namespace SessionLock {
class BrowsingGuard : public QObject, public LockGuard
{
    Q_OBJECT
public:
    explicit BrowsingGuard(SettingsManager* settings, LayoutProvider* layouts, QString settingsPath = {},
                           QString layoutsPath = {});
    ~BrowsingGuard() override;
    QString id() const override;
    bool enter(RecoveryJournal& journal, QString& error) override;
    bool leave(QString& error) override;
    bool recover(const RecoveryRecord& record, QString& error) override;

Q_SIGNALS:
    void protectionFailed(const QString& error);

private Q_SLOTS:
    void enforceWidgets();

private:
    bool eventFilter(QObject* receiver, QEvent* event) override;
    bool protect(FyWidget* widget, QString& error);
    bool persistCursor(bool value, QString& error);
    bool protectQuickSearch(QString& error);
    bool persistQuickSearchMode(int mode, QString& error);
    bool restoreLayouts(const RecoveryRecord& record, QString& error);
    static void setWidgetValue(FyWidget* widget, const QVariant& value);

    QPointer<SettingsManager> m_settings;
    QPointer<LayoutProvider> m_layouts;
    QString m_settingsPath;
    QString m_layoutsPath;
    RecoveryJournal* m_journal{nullptr};
    struct ObservedWidget {
        QPointer<FyWidget> widget;
        bool persistId;
    };
    std::vector<ObservedWidget> m_widgets;
    struct SearchEditor {
        QPointer<QLineEdit> editor;
        bool blocked;
    };
    std::vector<SearchEditor> m_searchEditors;
    bool m_updating{false};
    bool m_entryScan{false};
};
} // namespace SessionLock
} // namespace Fooyin

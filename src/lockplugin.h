// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <core/plugins/coreplugin.h>
#include <core/plugins/plugin.h>
#include <gui/plugins/guiplugin.h>

#include <QObject>

#include <memory>

namespace Fooyin::SessionLock {
class LockRuntime;

class LockPlugin : public QObject, public Plugin, public CorePlugin, public GuiPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID FOOYIN_PLUGIN_IID FILE "lock.json")
    Q_INTERFACES(Fooyin::Plugin Fooyin::CorePlugin Fooyin::GuiPlugin)

public:
    LockPlugin();
    ~LockPlugin() override;

    void initialise(const CorePluginContext& context) override;
    void initialise(const GuiPluginContext& context) override;
    void shutdown() override;

private:
    std::unique_ptr<LockRuntime> m_runtime;
    QMetaObject::Connection m_quitConnection;
    bool m_shutdown{false};
};
} // namespace Fooyin::SessionLock

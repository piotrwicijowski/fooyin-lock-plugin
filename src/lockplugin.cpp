// SPDX-License-Identifier: GPL-3.0-or-later
#include "lockplugin.h"

#include "lockruntime.h"

#include <QCoreApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(LOCK_PLUGIN, "fy.sessionlock", QtWarningMsg)

namespace Fooyin::SessionLock {
LockPlugin::LockPlugin()
    : m_runtime{std::make_unique<LockRuntime>()}
{
    if(auto* app = QCoreApplication::instance()) {
        m_quitConnection = connect(app, &QCoreApplication::aboutToQuit, this, &LockPlugin::shutdown);
    }
}

LockPlugin::~LockPlugin()
{
    shutdown();
}

void LockPlugin::initialise(const CorePluginContext& context)
{
    if(!m_shutdown && m_runtime->initialiseCore(context)) {
        qCDebug(LOCK_PLUGIN) << "Core services acquired";
    }
}

void LockPlugin::initialise(const GuiPluginContext& context)
{
    if(!m_shutdown && m_runtime->initialiseGui(context)) {
        qCDebug(LOCK_PLUGIN) << "GUI services acquired";
    }
}

void LockPlugin::shutdown()
{
    if(m_shutdown) {
        return;
    }
    m_shutdown = true;
    QObject::disconnect(m_quitConnection);
    m_runtime.reset();
    qCDebug(LOCK_PLUGIN) << "Shutdown complete";
}
} // namespace Fooyin::SessionLock

// SPDX-License-Identifier: GPL-3.0-or-later
#include "contexts.h"
#include "lockruntime.h"

#include <core/plugins/coreplugin.h>
#include <core/plugins/plugin.h>
#include <gui/plugins/guiplugin.h>
#include <utils/actions/actionmanager.h>
#include <utils/settings/settingsmanager.h>

#include <QApplication>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMainWindow>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Fooyin;
using namespace Fooyin::SessionLock;
using namespace Fooyin::SessionLock::Tests;

namespace {
QString pluginPath;
QStringList messages;
QtMessageHandler previousHandler{nullptr};

void captureMessages(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if(QString::fromLatin1(context.category) == QStringLiteral("fy.sessionlock")) {
        messages.append(message);
    }
    else if(previousHandler) {
        previousHandler(type, context, message);
    }
}
}

class PluginLifecycleTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        messages.clear();
        previousHandler = qInstallMessageHandler(captureMessages);
        QLoggingCategory::setFilterRules(QStringLiteral("fy.sessionlock.debug=true"));
    }

    void cleanup()
    {
        qInstallMessageHandler(previousHandler);
        QLoggingCategory::setFilterRules({});
    }

    void metadata()
    {
        QPluginLoader loader{pluginPath};
        QCOMPARE(loader.metaData().value(QStringLiteral("IID")).toString(), QString::fromLatin1(FOOYIN_PLUGIN_IID));
        const auto metadata = loader.metaData().value(QStringLiteral("MetaData")).toObject();
        QCOMPARE(metadata.value(QStringLiteral("Name")).toString(), QStringLiteral("Session Lock"));
        QVERIFY(!metadata.value(QStringLiteral("Version")).toString().isEmpty());
    }

    void lifecycle_data()
    {
        QTest::addColumn<int>("stage");
        QTest::addColumn<bool>("explicitShutdown");
        for(int stage = 0; stage <= 2; ++stage) {
            QTest::newRow(qPrintable(QStringLiteral("stage-%1-shutdown").arg(stage))) << stage << true;
            QTest::newRow(qPrintable(QStringLiteral("stage-%1-destructor").arg(stage))) << stage << false;
        }
    }

    void lifecycle()
    {
        QFETCH(int, stage);
        QFETCH(bool, explicitShutdown);
        QPluginLoader loader{pluginPath};
        loader.setLoadHints({}); // Exercise actual unload rather than Qt's default pinning.
        QPointer<QObject> root = loader.instance();
        QVERIFY2(root, qPrintable(loader.errorString()));
        auto* plugin = qobject_cast<Plugin*>(root);
        auto* core = qobject_cast<CorePlugin*>(root);
        auto* gui = qobject_cast<GuiPlugin*>(root);
        QVERIFY(plugin && core && gui);
        if(stage >= 1) {
            core->initialise(coreContext());
            core->initialise(coreContext());
        }
        if(stage >= 2) {
            gui->initialise(guiContext());
            gui->initialise(guiContext());
        }
        if(explicitShutdown) {
            plugin->shutdown();
            plugin->shutdown();
            core->initialise(coreContext());
            gui->initialise(guiContext()); // A stopped plugin cannot be resurrected.
        }
        QVERIFY(loader.unload());
        QVERIFY(root.isNull());
        QCoreApplication::processEvents();
        QCOMPARE(messages.count(QStringLiteral("Core services acquired")), stage >= 1 ? 1 : 0);
        QCOMPARE(messages.count(QStringLiteral("GUI services acquired")), stage >= 2 ? 1 : 0);
        QCOMPARE(messages.count(QStringLiteral("Shutdown complete")), 1);
        QCOMPARE(messages.size(), stage + 1);
    }

    void guiBeforeCore()
    {
        QPluginLoader loader{pluginPath};
        loader.setLoadHints({});
        auto* root = loader.instance();
        QVERIFY2(root, qPrintable(loader.errorString()));
        auto* core = qobject_cast<CorePlugin*>(root);
        auto* gui = qobject_cast<GuiPlugin*>(root);
        QVERIFY(core && gui);
        gui->initialise(guiContext());
        QVERIFY(messages.isEmpty());
        core->initialise(coreContext());
        gui->initialise(guiContext());
        QVERIFY(loader.unload());
        QCOMPARE(messages.size(), 3);
    }

    void borrowedServicesAndCallbackLifetime()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QMainWindow window;
        auto settings = std::make_unique<SettingsManager>(dir.filePath(QStringLiteral("settings.ini")));
        auto actions = std::make_unique<ActionManager>(settings.get());
        actions->setMainWindow(&window);
        auto core = coreContext();
        auto gui = guiContext();
        core.settingsManager = settings.get();
        gui.actionManager = actions.get();
        auto runtime = std::make_unique<LockRuntime>();
        QVERIFY(runtime->initialiseCore(core));
        QVERIFY(runtime->initialiseGui(gui));
        QCOMPARE(runtime->services().settings.data(), settings.get());
        QCOMPARE(runtime->services().actions.data(), actions.get());
        actions.reset();
        settings.reset();
        QVERIFY(runtime->services().settings.isNull());
        QVERIFY(runtime->services().actions.isNull());

        // Guard callbacks must use this runtime (or a child guard) as receiver.
        // Queued work and QObject-owned guard children must not survive teardown.
        bool called{false};
        QTimer::singleShot(0, runtime.get(), [&called]() { called = true; });
        QPointer<QObject> guard = new QObject(runtime.get());
        runtime.reset();
        QVERIFY(guard.isNull());
        QCoreApplication::processEvents();
        QVERIFY(!called);
    }

    void applicationQuit()
    {
        QPluginLoader loader{pluginPath};
        loader.setLoadHints({});
        auto* root = loader.instance();
        QVERIFY2(root, qPrintable(loader.errorString()));
        auto* core = qobject_cast<CorePlugin*>(root);
        auto* gui = qobject_cast<GuiPlugin*>(root);
        QVERIFY(core && gui);
        core->initialise(coreContext());
        gui->initialise(guiContext());
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        QCOMPARE(qApp->exec(), 0);
        QCOMPARE(messages.count(QStringLiteral("Shutdown complete")), 1);
        QVERIFY(loader.unload());
        QCOMPARE(messages.count(QStringLiteral("Shutdown complete")), 1);
    }
};

int main(int argc, char** argv)
{
    QApplication app{argc, argv};
    if(argc < 2) {
        qCritical("Pass the plugin library path as the first argument");
        return 1;
    }
    pluginPath = QString::fromLocal8Bit(argv[1]);
    for(int i = 1; i < argc; ++i) {
        argv[i] = argv[i + 1];
    }
    --argc;
    PluginLifecycleTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "pluginlifecycletest.moc"

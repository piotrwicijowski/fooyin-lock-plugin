create_fooyin_plugin(LockSmokeDriver NO_INSTALL
    DEPENDS LockRuntime
    SOURCES smokedriver.cpp widgetinputchecks.cpp widgetinputchecks.h playbackchecks.cpp playbackchecks.h
        seekchecks.cpp seekchecks.h)
target_compile_definitions(LockSmokeDriver PRIVATE QT_NO_KEYWORDS)

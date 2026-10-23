create_fooyin_plugin(LockSmokeDriver NO_INSTALL
    DEPENDS LockRuntime
    SOURCES smokedriver.cpp widgetinputchecks.cpp widgetinputchecks.h playbackchecks.cpp playbackchecks.h
        seekchecks.cpp seekchecks.h browsingchecks.cpp browsingchecks.h
        compatibilitychecks.cpp compatibilitychecks.h)
target_compile_definitions(LockSmokeDriver PRIVATE QT_NO_KEYWORDS)
target_link_libraries(LockSmokeDriver PRIVATE Qt6::Test)

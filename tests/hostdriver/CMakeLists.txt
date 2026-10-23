create_fooyin_plugin(LockSmokeDriver NO_INSTALL
    DEPENDS Fooyin::Core Fooyin::Gui Qt6::Widgets
    SOURCES smokedriver.cpp)
target_compile_definitions(LockSmokeDriver PRIVATE QT_NO_KEYWORDS)

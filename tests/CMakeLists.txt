add_executable(lock_lifecycle_test pluginlifecycletest.cpp)
target_link_libraries(lock_lifecycle_test PRIVATE LockRuntime Qt6::Test)
add_dependencies(lock_lifecycle_test Lock)
add_test(NAME lock.lifecycle COMMAND lock_lifecycle_test $<TARGET_FILE:Lock>)
set_tests_properties(lock.lifecycle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

add_executable(lock_coordinator_test coordinatortest.cpp)
target_link_libraries(lock_coordinator_test PRIVATE LockRuntime Qt6::Test)
add_test(NAME lock.coordinator COMMAND lock_coordinator_test)
set_tests_properties(lock.coordinator PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

add_executable(lock_action_test actionguardtest.cpp)
target_link_libraries(lock_action_test PRIVATE LockRuntime Qt6::Test)
add_test(NAME lock.actions COMMAND lock_action_test)
set_tests_properties(lock.actions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

add_executable(lock_playlist_test playlistguardtest.cpp)
target_link_libraries(lock_playlist_test PRIVATE LockRuntime Qt6::Test)
add_test(NAME lock.playlists COMMAND lock_playlist_test)
set_tests_properties(lock.playlists PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

add_executable(lock_widget_input_test widgetinputfiltertest.cpp ../src/widgetinputfilter.cpp)
target_include_directories(lock_widget_input_test PRIVATE ../src)
target_link_libraries(lock_widget_input_test PRIVATE Qt6::Widgets Qt6::Test)
target_compile_definitions(lock_widget_input_test PRIVATE QT_NO_KEYWORDS)
add_test(NAME lock.widget-input COMMAND lock_widget_input_test)
set_tests_properties(lock.widget-input PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

# A separate, opt-in driver closes the real host via QApplication::quit().
# It is never part of the installed production plugin.
add_subdirectory(hostdriver)
set(LOCK_FOOYIN_EXECUTABLE "" CACHE FILEPATH "Matching fooyin executable for the real-host smoke test")
if(LOCK_FOOYIN_EXECUTABLE)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    add_test(NAME lock.host-smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/host-smoke.py"
            --host "${LOCK_FOOYIN_EXECUTABLE}"
            --plugin $<TARGET_FILE:Lock>
            --driver $<TARGET_FILE:LockSmokeDriver>)
    set_tests_properties(lock.host-smoke PROPERTIES TIMEOUT 100)
endif()

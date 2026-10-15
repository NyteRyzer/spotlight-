find_package(Qt6 REQUIRED COMPONENTS Test)

function(spotlight_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE spotlight_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

spotlight_add_test(test_timeline)
spotlight_add_test(test_project)
spotlight_add_test(test_exporter)

# GUI smoke test: builds the whole main window offscreen, imports generated
# media, edits the timeline, and exercises frame capture into the Design Studio.
add_executable(test_ui test_ui.cpp)
target_link_libraries(test_ui PRIVATE spotlight_ui Qt6::Test)
add_test(NAME test_ui COMMAND test_ui)
set_tests_properties(test_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

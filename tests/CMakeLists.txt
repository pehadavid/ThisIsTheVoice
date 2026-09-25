add_executable(titv_tests main.cpp test_parameters.cpp test_engine.cpp test_dsp.cpp test_effects.cpp test_presets.cpp test_ui_texts.cpp
  test_user_presets.cpp
  ${PROJECT_SOURCE_DIR}/src/ui/Settings.cpp
  ${PROJECT_SOURCE_DIR}/src/ui/UserPresets.cpp
  ${PROJECT_SOURCE_DIR}/src/ui/PresetBrowser.cpp)
target_link_libraries(titv_tests PRIVATE titv_engine)
target_compile_options(titv_tests PRIVATE ${TITV_WARNINGS})
add_test(NAME engine COMMAND titv_tests)

add_executable(titv_bench bench_engine.cpp)
target_link_libraries(titv_bench PRIVATE titv_engine)
target_compile_options(titv_bench PRIVATE ${TITV_WARNINGS})

add_executable(titv_measure measure.cpp)
target_link_libraries(titv_measure PRIVATE titv_engine)
target_compile_options(titv_measure PRIVATE ${TITV_WARNINGS})

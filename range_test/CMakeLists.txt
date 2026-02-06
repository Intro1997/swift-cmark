add_executable(range_test
  main.c
)

# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(MyProject)

include_directories(
  ${PROJECT_SOURCE_DIR}/src/include
  ${PROJECT_BINARY_DIR}/src
  ${PROJECT_SOURCE_DIR}/extensions/include
  ${PROJECT_BINARY_DIR}/extensions
)
target_link_libraries(range_test PRIVATE
  libcmark-gfm
  libcmark-gfm-extensions)

add_test(NAME range_test COMMAND range_test)
if(WIN32 AND BUILD_SHARED_LIBS)
  set_tests_properties(range_test PROPERTIES
    ENVIRONMENT "PATH=$<TARGET_FILE_DIR:libcmark-gfm>;$<TARGET_FILE_DIR:libcmark-gfm-extensions>;$ENV{PATH}")
endif()

set(CLANG_FORMAT_FILE_EXTENSIONS ${CLANG_FORMAT_FILE_EXTENSIONS} *.cpp *.h *.hpp *.cc)
file(GLOB_RECURSE SOURCE_FILES_TO_FORMAT ${CLANG_FORMAT_FILE_EXTENSIONS})

set(CLANG_FORMAT_EXCLUDE_PATTERNS ${CLANG_FORMAT_EXCLUDE_PATTERNS} "/CMakeFiles/" "cmake" "build/")

foreach (SOURCE_FILE ${SOURCE_FILES_TO_FORMAT})
  foreach (EXCLUDE_PATTERN ${CLANG_FORMAT_EXCLUDE_PATTERNS})
    string (FIND ${SOURCE_FILE} ${EXCLUDE_PATTERN} EXCLUDE_FOUND)
    if (NOT ${EXCLUDE_FOUND} EQUAL -1)
      list (REMOVE_ITEM SOURCE_FILES_TO_FORMAT ${SOURCE_FILE})
    endif ()
  endforeach()
endforeach()

if(CLANG_FORMAT_EXE)
  add_custom_target(
    format-all
    COMMENT "Formatting all source files"
    COMMAND ${CLANG_FORMAT_EXE} -i -style=file ${FILES_TO_FORMAT}
  )
  add_custom_target(
    format-check
    COMMENT "Checking code style format of changed source files"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${CMAKE_CURRENT_LIST_DIR}/../script/clang-format-changed.py --check-only --clang-format-bin ${CLANG_FORMAT_EXE}
  )
  add_custom_target(
    format
    COMMENT "Format code style of changed source files"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${CMAKE_CURRENT_LIST_DIR}/../script/clang-format-changed.py --in-place --clang-format-bin ${CLANG_FORMAT_EXE}
  )
endif()

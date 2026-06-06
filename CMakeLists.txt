cmake_minimum_required(VERSION 3.20)

project(HP_CLEANER++
    VERSION 1.0.0
    DESCRIPTION "Windows system cleaner and optimizer"
    LANGUAGES CXX RC
)

if(NOT WIN32)
    message(FATAL_ERROR "HP CLEANER++ supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HP_CLEANER_COPY_RUNTIME_ASSETS "Copy config/i18n next to the executable after build" ON)

include(cmake/HP_CLEANERSources.cmake)
include(cmake/HP_CLEANERTarget.cmake)

if(HP_CLEANER_COPY_RUNTIME_ASSETS)
    add_custom_command(TARGET HP_CLEANER POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/config/i18n"
            "$<TARGET_FILE_DIR:HP_CLEANER>/i18n"
        COMMENT "Copying runtime i18n packs"
    )
endif()

message(STATUS "HP CLEANER++ ${PROJECT_VERSION} | ${CMAKE_BUILD_TYPE}")

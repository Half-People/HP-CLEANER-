add_executable(HP_CLEANER WIN32
    ${HP_CLEANER_CPP}
    ${HP_IMGUI_CPP}
    ${HP_IMPLOT_CPP}
    ${HP_CLEANER_RC}
)

set_target_properties(HP_CLEANER PROPERTIES
    OUTPUT_NAME "HP_CLEANER++"
    WIN32_EXECUTABLE TRUE
)

target_include_directories(HP_CLEANER PRIVATE
    ${HP_CLEANER_INCLUDE_DIRS}
    ${HP_THIRD_PARTY_INCLUDE_DIRS}
)

target_compile_definitions(HP_CLEANER PRIVATE
    SPDLOG_HEADER_ONLY
    _WINDOWS
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

if(MSVC)
    target_link_options(HP_CLEANER PRIVATE "/ENTRY:mainCRTStartup")
    target_compile_options(HP_CLEANER PRIVATE
        /utf-8
        /Zm1000
        /FS
        /W3
        /permissive-
    )
    target_compile_options(HP_CLEANER PRIVATE
        $<$<CONFIG:Release>:/MT>
    )
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/src/i18n/Hi18nBuiltinPages.cpp"
        PROPERTIES COMPILE_FLAGS "/bigobj"
    )
endif()

target_link_libraries(HP_CLEANER PRIVATE
    d3d9
    dbghelp
    psapi
    shell32
    ole32
    version
    comctl32
    shlwapi
)

if(MSVC)
    set_property(TARGET HP_CLEANER PROPERTY MSVC_RUNTIME_LIBRARY
        "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<CONFIG:Release>:MultiThreaded>"
    )
endif()

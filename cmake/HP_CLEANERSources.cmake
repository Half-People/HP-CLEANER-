file(GLOB_RECURSE HP_CLEANER_CPP CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*/*.cpp"
)

set(HP_IMGUI_CPP
    "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_demo.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_impl_dx9.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_impl_win32.cpp"
)

set(HP_IMPLOT_CPP
    "${CMAKE_SOURCE_DIR}/third_party/implot/implot.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/implot/implot_demo.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/implot/implot_items.cpp"
)

set(HP_CLEANER_RC "${CMAKE_SOURCE_DIR}/HP_CLEANER++.rc")

file(GLOB HP_SRC_MODULE_DIRS LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/src/*")
set(HP_CLEANER_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
    ${HP_SRC_MODULE_DIRS}
)

set(HP_THIRD_PARTY_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/third_party/imgui"
    "${CMAKE_SOURCE_DIR}/third_party/implot"
    "${CMAKE_SOURCE_DIR}/third_party/json/include"
    "${CMAKE_SOURCE_DIR}/third_party/spdlog/include"
    "${CMAKE_SOURCE_DIR}/third_party/stb"
)

list(LENGTH HP_CLEANER_CPP HP_CLEANER_CPP_COUNT)
list(LENGTH HP_IMGUI_CPP HP_IMGUI_CPP_COUNT)
list(LENGTH HP_IMPLOT_CPP HP_IMPLOT_CPP_COUNT)
math(EXPR HP_TOTAL_SRC "${HP_CLEANER_CPP_COUNT}+${HP_IMGUI_CPP_COUNT}+${HP_IMPLOT_CPP_COUNT}")
message(STATUS "HP CLEANER sources: ${HP_TOTAL_SRC} translation units (app=${HP_CLEANER_CPP_COUNT}, imgui=${HP_IMGUI_CPP_COUNT}, implot=${HP_IMPLOT_CPP_COUNT})")

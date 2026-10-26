cmake_minimum_required(VERSION 3.22.1)

project(AlgorithmHub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(algorithm_hub
    src/main.cpp
)

find_package(PkgConfig REQUIRED)

pkg_check_modules(MYSQL REQUIRED mysqlclient)

target_include_directories(algorithm_hub PRIVATE
    ${MYSQL_INCLUDE_DIRS}
)

target_link_libraries(algorithm_hub PRIVATE
    ${MYSQL_LIBRARIES}
)

pkg_check_modules(HIREDIS REQUIRED hiredis)

target_include_directories(algorithm_hub PRIVATE
    ${HIREDIS_INCLUDE_DIRS}
)

target_link_libraries(algorithm_hub PRIVATE
    ${HIREDIS_LIBRARIES}
)

find_package(OpenSSL REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc i18n)
target_link_libraries(algorithm_hub PRIVATE ICU::uc ICU::i18n)

target_link_libraries(algorithm_hub PRIVATE
    OpenSSL::Crypto
)

# Ubuntu 的 libhpdf-dev 不提供 pkg-config 配置，因此直接查找头文件和库。
find_path(HPDF_INCLUDE_DIR NAMES hpdf.h REQUIRED)
find_library(HPDF_LIBRARY NAMES hpdf REQUIRED)

# 三种字体分别服务于正文、标题和代码；粗体与等宽字体留空时会回退到正文。
set(ALGORITHM_HUB_PDF_REGULAR_FONT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LXGWWenKai-Regular.ttf" CACHE FILEPATH
    "Path to an embeddable Chinese TrueType regular font for PDF export")
set(ALGORITHM_HUB_PDF_BOLD_FONT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LXGWWenKai-Medium.ttf" CACHE FILEPATH
    "Path to an embeddable Chinese TrueType bold font for PDF export")
set(ALGORITHM_HUB_PDF_MONO_FONT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LXGWWenKaiMono-Regular.ttf" CACHE FILEPATH
    "Path to an embeddable Chinese TrueType monospace font for PDF export")
set(ALGORITHM_HUB_PDF_LOGO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/algorithm-hub-logo.png" CACHE FILEPATH
    "Path to the Algorithm Hub PDF cover logo")

target_include_directories(algorithm_hub PRIVATE
    ${HPDF_INCLUDE_DIR}
)

target_link_libraries(algorithm_hub PRIVATE
    ${HPDF_LIBRARY}
)

target_compile_definitions(algorithm_hub PRIVATE
    ALGORITHM_HUB_CONFIG_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    ALGORITHM_HUB_WEB_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/web"
    ALGORITHM_HUB_PDF_REGULAR_FONT_PATH="${ALGORITHM_HUB_PDF_REGULAR_FONT_PATH}"
    ALGORITHM_HUB_PDF_BOLD_FONT_PATH="${ALGORITHM_HUB_PDF_BOLD_FONT_PATH}"
    ALGORITHM_HUB_PDF_MONO_FONT_PATH="${ALGORITHM_HUB_PDF_MONO_FONT_PATH}"
    ALGORITHM_HUB_PDF_LOGO_PATH="${ALGORITHM_HUB_PDF_LOGO_PATH}"
)

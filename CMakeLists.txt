cmake_minimum_required(VERSION 3.21)
project(PDFOrganizer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Find Qt6 ──────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core Widgets Sql Concurrent Network WebSockets
)

# Optional: Qt6::Pdf for thumbnail generation (Qt 6.4+)
find_package(Qt6 QUIET COMPONENTS Pdf)

find_package(Qt6 REQUIRED COMPONENTS WebSockets)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp

    src/api/apiclient.cpp
    src/api/apitypes.cpp

    src/models/pdffile.cpp
    src/models/pdfmodel.cpp
    src/models/tagmodel.cpp
    src/models/foldermodel.cpp

    src/database/databasemanager.cpp

    src/controllers/folderwatcher.cpp
    src/controllers/pdfcontroller.cpp
    src/controllers/tagcontroller.cpp

    src/views/folderpanel.cpp
    src/views/listview.cpp
    src/views/gridview.cpp
    src/views/recentview.cpp
    src/views/tagmanagerdialog.cpp
    src/views/settingsdialog.cpp
    src/views/logindialog.cpp
    src/views/joingroupdialog.cpp

    src/delegates/listdelegate.cpp
    src/delegates/griddelegate.cpp

    src/utils/pdfopener.cpp
    src/utils/thumbnailgenerator.cpp
    src/utils/searchfilterproxy.cpp

    resources/resources.qrc
)

set(HEADERS
    src/mainwindow.h
    src/api/apiclient.h
    src/api/apitypes.h
    src/models/pdffile.h
    src/models/pdfmodel.h
    src/models/tagmodel.h
    src/models/foldermodel.h
    src/database/databasemanager.h
    src/controllers/folderwatcher.h
    src/controllers/pdfcontroller.h
    src/controllers/tagcontroller.h
    src/views/folderpanel.h
    src/views/listview.h
    src/views/gridview.h
    src/views/recentview.h
    src/views/tagmanagerdialog.h
    src/views/settingsdialog.h
    src/views/logindialog.h
    src/views/joingroupdialog.h
    src/delegates/listdelegate.h
    src/delegates/griddelegate.h
    src/utils/pdfopener.h
    src/utils/thumbnailgenerator.h
    src/utils/searchfilterproxy.h
)

# ── Application icon ─────────────────────────────────────────────────────────
# Single source of truth for the app icon; platform-native formats are
# generated from it at build time so there's nothing extra to keep in sync.
set(APP_ICON_PNG ${CMAKE_SOURCE_DIR}/assets/icon.png)
set(APP_ICON_GEN_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${APP_ICON_GEN_DIR})

if(WIN32)
    find_program(IMAGEMAGICK_CONVERT NAMES magick convert)
    if(IMAGEMAGICK_CONVERT)
        set(APP_ICON_ICO ${APP_ICON_GEN_DIR}/app.ico)
        set(APP_ICON_RC ${APP_ICON_GEN_DIR}/app_icon.rc)
        add_custom_command(
            OUTPUT ${APP_ICON_ICO}
            COMMAND ${IMAGEMAGICK_CONVERT} ${APP_ICON_PNG}
                    -define icon:auto-resize=256,128,64,48,32,16
                    ${APP_ICON_ICO}
            DEPENDS ${APP_ICON_PNG}
            COMMENT "Generating Windows app icon (.ico) from assets/icon.png"
        )
        file(WRITE ${APP_ICON_RC} "IDI_ICON1 ICON \"${APP_ICON_ICO}\"\n")
        set_source_files_properties(${APP_ICON_RC} PROPERTIES OBJECT_DEPENDS ${APP_ICON_ICO})
        list(APPEND SOURCES ${APP_ICON_RC})
    else()
        message(WARNING "ImageMagick not found — the Windows .exe will not have an embedded icon")
    endif()
elseif(APPLE)
    find_program(SIPS_EXE sips)
    find_program(ICONUTIL_EXE iconutil)
    if(SIPS_EXE AND ICONUTIL_EXE)
        set(APP_ICONSET_DIR ${APP_ICON_GEN_DIR}/app.iconset)
        set(APP_ICON_ICNS ${APP_ICON_GEN_DIR}/app.icns)
        add_custom_command(
            OUTPUT ${APP_ICON_ICNS}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_ICONSET_DIR}
            COMMAND ${SIPS_EXE} -z 16   16   ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_16x16.png
            COMMAND ${SIPS_EXE} -z 32   32   ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_16x16@2x.png
            COMMAND ${SIPS_EXE} -z 32   32   ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_32x32.png
            COMMAND ${SIPS_EXE} -z 64   64   ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_32x32@2x.png
            COMMAND ${SIPS_EXE} -z 128  128  ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_128x128.png
            COMMAND ${SIPS_EXE} -z 256  256  ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_128x128@2x.png
            COMMAND ${SIPS_EXE} -z 256  256  ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_256x256.png
            COMMAND ${SIPS_EXE} -z 512  512  ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_256x256@2x.png
            COMMAND ${SIPS_EXE} -z 512  512  ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_512x512.png
            COMMAND ${SIPS_EXE} -z 1024 1024 ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_512x512@2x.png
            COMMAND ${ICONUTIL_EXE} -c icns ${APP_ICONSET_DIR} -o ${APP_ICON_ICNS}
            DEPENDS ${APP_ICON_PNG}
            COMMENT "Generating macOS app icon (.icns) from assets/icon.png"
        )
        set_source_files_properties(${APP_ICON_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        list(APPEND SOURCES ${APP_ICON_ICNS})
    else()
        message(WARNING "sips/iconutil not found — the macOS .app bundle will not have an icon")
    endif()
endif()

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(PDFOrganizer ${SOURCES} ${HEADERS})

target_include_directories(PDFOrganizer PRIVATE src)

target_link_libraries(PDFOrganizer PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Network
    Qt6::WebSockets
)

if(Qt6Pdf_FOUND)
    target_link_libraries(PDFOrganizer PRIVATE Qt6::Pdf)
    target_compile_definitions(PDFOrganizer PRIVATE HAVE_QT_PDF)
    message(STATUS "Qt6::Pdf found — native thumbnail generation enabled")
else()
    message(STATUS "Qt6::Pdf not found — placeholder thumbnails will be used")
endif()

# ── Platform-specific ─────────────────────────────────────────────────────────
if(WIN32)
    set_target_properties(PDFOrganizer PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set_target_properties(PDFOrganizer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE app.icns
    )
endif()

# ── API smoke test ────────────────────────────────────────────────────────────
# Exercises ApiClient against a running backend. Off by default because it
# needs a live server:
#   cmake -S . -B build -DBUILD_API_SMOKETEST=ON
#   ./build/apiclient_smoketest http://localhost:8000
option(BUILD_API_SMOKETEST "Build the ApiClient integration smoke test" OFF)

if(BUILD_API_SMOKETEST)
    add_executable(apiclient_smoketest
        tests/apiclient_smoketest.cpp
        src/api/apiclient.cpp
        src/api/apitypes.cpp
        src/api/apiclient.h
        src/api/apitypes.h
    )
    target_include_directories(apiclient_smoketest PRIVATE src)
    target_link_libraries(apiclient_smoketest PRIVATE Qt6::Core Qt6::Network Qt6::WebSockets)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS PDFOrganizer DESTINATION bin)

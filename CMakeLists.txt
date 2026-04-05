cmake_minimum_required(VERSION 3.21)
project(PDFOrganizer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Find Qt6 ──────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core Widgets Sql Concurrent
)

# Optional: Qt6::Pdf for thumbnail generation (Qt 6.4+)
find_package(Qt6 QUIET COMPONENTS Pdf)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp

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

    src/delegates/listdelegate.cpp
    src/delegates/griddelegate.cpp

    src/utils/pdfopener.cpp
    src/utils/thumbnailgenerator.cpp
    src/utils/searchfilterproxy.cpp

    resources/resources.qrc
)

set(HEADERS
    src/mainwindow.h
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
    src/delegates/listdelegate.h
    src/delegates/griddelegate.h
    src/utils/pdfopener.h
    src/utils/thumbnailgenerator.h
    src/utils/searchfilterproxy.h
)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(PDFOrganizer ${SOURCES} ${HEADERS})

target_include_directories(PDFOrganizer PRIVATE src)

target_link_libraries(PDFOrganizer PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Sql
    Qt6::Concurrent
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
    set_target_properties(PDFOrganizer PROPERTIES MACOSX_BUNDLE TRUE)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS PDFOrganizer DESTINATION bin)

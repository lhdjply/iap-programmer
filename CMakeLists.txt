cmake_minimum_required(VERSION 3.16)

# 使用变量定义项目名/目标名
set(PROJECT_TARGET iap-programmer)

# 从VERSION文件读取版本号
file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" PROJECT_VERSION_STRING LIMIT_COUNT 1)
string(REPLACE "." ";" VERSION_LIST ${PROJECT_VERSION_STRING})
list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)
list(GET VERSION_LIST 2 PROJECT_VERSION_PATCH)

project(${PROJECT_TARGET} VERSION ${PROJECT_VERSION_STRING} LANGUAGES CXX C)

# 定义版本号宏，供代码使用
add_definitions(-DPROJECT_VERSION="${PROJECT_VERSION_STRING}")
add_definitions(-DPROJECT_TARGET="${PROJECT_TARGET}")

option (TRANSLATION_RESOURCE_EMBEDDING OFF)

set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加编译器标志以禁用特定警告
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-stringop-overflow")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-stringop-overflow")

# 添加RC语言支持
if(WIN32)
    enable_language(RC)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(PkgConfig REQUIRED)

find_library(HIDAPI_LIBRARY NAMES hidapi hidapi-libusb)
find_path(HIDAPI_INCLUDE_DIR NAMES hidapi.h PATH_SUFFIXES hidapi)

set (IAP_PROGRAMMER_QRC_FILES
    assets/resources.qrc
)

qt6_standard_project_setup(
    I18N_TRANSLATED_LANGUAGES en zh_CN
)

# Windows图标资源文件
if(WIN32)
    set(WIN_RESOURCES "${CMAKE_SOURCE_DIR}/assets/icons/windows/win_resources.rc")
endif()

# 自动查找源文件
file(GLOB_RECURSE PROJECT_SOURCES
    "src/*.cpp"
    "src/*.h"
)

include_directories(src)

# 使用变量创建目标
qt_add_executable(${PROJECT_TARGET}
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    ${IAP_PROGRAMMER_QRC_FILES}
    ${WIN_RESOURCES}
)

# 使用变量链接库
target_link_libraries(${PROJECT_TARGET} PRIVATE Qt${QT_VERSION_MAJOR}::Widgets ${HIDAPI_LIBRARY})
target_include_directories(${PROJECT_TARGET} PRIVATE ${HIDAPI_INCLUDE_DIR})
target_compile_options(${PROJECT_TARGET} PRIVATE -Wall -Wextra -Wpedantic)

# 使用变量设置属性
set_target_properties(${PROJECT_TARGET} PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if (TRANSLATION_RESOURCE_EMBEDDING)
    qt6_add_translations(${PROJECT_TARGET}
        TS_FILE_DIR assets/translations
    )
    target_compile_definitions(${PROJECT_TARGET}
        PRIVATE TRANSLATION_RESOURCE_EMBEDDING
    )
elseif (WIN32)
    set_target_properties(${PROJECT_TARGET} PROPERTIES
        RC_ICONS "${CMAKE_SOURCE_DIR}/assets/icons/windows/${PROJECT_TARGET}.ico"
    )

    qt6_add_translations(${PROJECT_TARGET}
        TS_FILE_DIR assets/translations
        QM_FILES_OUTPUT_VARIABLE PROJECT_QM_FILES
    )
    target_sources(${PROJECT_TARGET} PRIVATE ${PROJECT_QM_FILES})
    set (QM_FILE_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}/translations")
elseif (APPLE)

elseif (UNIX)
    set(ICON16 assets/icons/linux/hicolor/16x16/apps/${PROJECT_TARGET}.png )
    set(ICON22 assets/icons/linux/hicolor/22x22/apps/${PROJECT_TARGET}.png )
    set(ICON32 assets/icons/linux/hicolor/32x32/apps/${PROJECT_TARGET}.png )
    set(ICON48 assets/icons/linux/hicolor/48x48/apps/${PROJECT_TARGET}.png )
    set(ICON64 assets/icons/linux/hicolor/64x64/apps/${PROJECT_TARGET}.png )
    set(ICON128 assets/icons/linux/hicolor/128x128/apps/${PROJECT_TARGET}.png )
    set(ICON256 assets/icons/linux/hicolor/256x256/apps/${PROJECT_TARGET}.png )
    set(ICON512 assets/icons/linux/hicolor/512x512/apps/${PROJECT_TARGET}.png )
    set(ICONsc assets/icons/linux/hicolor/scalable/apps/${PROJECT_TARGET}.svg )

    install( FILES ${ICON16} DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/16x16/apps)
    install( FILES ${ICON22} DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/22x22/apps)
    install( FILES ${ICON32} DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/32x32/apps)
    install( FILES ${ICON48} DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/48x48/apps)
    install( FILES ${ICON64} DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/64x64/apps)
    install( FILES ${ICON128} DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/128x128/apps)
    install( FILES ${ICON256} DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/256x256/apps)
    install( FILES ${ICON512} DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/512x512/apps)
    install( FILES ${ICONsc} DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/scalable/apps)

    install(FILES ${CMAKE_SOURCE_DIR}/assets/desktop/${PROJECT_TARGET}.desktop DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/applications)

    qt6_add_translations(${PROJECT_TARGET}
        TS_FILE_DIR assets/translations
        QM_FILES_OUTPUT_VARIABLE PROJECT_QM_FILES
    )
    target_sources(${PROJECT_TARGET} PRIVATE ${PROJECT_QM_FILES})
    set (QM_FILE_INSTALL_DIR "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_TARGET}/translations")
    target_compile_definitions(${PROJECT_TARGET} PRIVATE QM_FILE_INSTALL_ABSOLUTE_DIR=${QM_FILE_INSTALL_DIR})
endif()

install( FILES ${PROJECT_QM_FILES} DESTINATION ${QM_FILE_INSTALL_DIR})

# 使用变量安装
include(GNUInstallDirs)
install(TARGETS ${PROJECT_TARGET}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 使用变量完成配置
qt_finalize_executable(${PROJECT_TARGET})

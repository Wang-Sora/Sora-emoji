cmake_minimum_required(VERSION 3.19)

project(Sora VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Multimedia
    Network
    Svg
    Pdf
    PdfWidgets
)

qt_standard_project_setup()

# Windows 下把 .ico 编进 exe：任务栏/资源管理器里显示的是 exe 自带的图标，
# 只在代码里 setWindowIcon 是不够的。rc 里的路径用 configure_file 注入绝对路径，
# 因为 windres 的相对路径是相对构建目录而不是 rc 文件所在目录。
if(WIN32)
    enable_language(RC)
    set(SORA_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
    configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
endif()

set(SORA_SOURCES
    main.cpp

    app/SoraMain.h
    app/SoraMain.cpp
    app/ThemeManager.h
    app/ThemeManager.cpp

    core/TintedIcon.h
    core/TintedIcon.cpp
    core/AudioPlayer.h
    core/AudioPlayer.cpp
    core/PdfController.h
    core/PdfController.cpp
    core/AiClient.h
    core/AiClient.cpp

    widgets/TitleBar.h
    widgets/TitleBar.cpp
    widgets/SideBar.h
    widgets/SideBar.cpp
    widgets/PlayerBar.h
    widgets/PlayerBar.cpp

    pages/MusicPage.h
    pages/MusicPage.cpp
    pages/PdfPage.h
    pages/PdfPage.cpp
    pages/AiPage.h
    pages/AiPage.cpp
    pages/SettingsPage.h
    pages/SettingsPage.cpp
)

set(SORA_RESOURCES
    resources/qss/dark.qss
    resources/qss/light.qss

    resources/icons/music.svg
    resources/icons/pdf.svg
    resources/icons/ai.svg
    resources/icons/settings.svg
    resources/icons/prev.svg
    resources/icons/play.svg
    resources/icons/pause.svg
    resources/icons/next.svg
    resources/icons/volume.svg
    resources/icons/search.svg
    resources/icons/send.svg
    resources/icons/open.svg
    resources/icons/sun.svg
    resources/icons/moon.svg
    resources/icons/zoom-in.svg
    resources/icons/zoom-out.svg
    resources/icons/fit-width.svg

    resources/app.png
)

if(WIN32)
    list(APPEND SORA_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

qt_add_executable(Sora
    WIN32 MACOSX_BUNDLE
    ${SORA_SOURCES}
)

qt_add_resources(Sora "sora_assets"
    PREFIX "/"
    BASE "resources"
    FILES ${SORA_RESOURCES}
)

target_include_directories(Sora PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Sora
    PRIVATE
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Multimedia
        Qt::Network
        Qt::Svg
        Qt::Pdf
        Qt::PdfWidgets
)

# 构建后自动把 Qt 运行库与插件铺到 exe 同目录，这样直接双击 Sora.exe 也能运行，
# 不会弹「找不到 Qt6Gui.dll」。放在 POST_BUILD 里的好处是不管用命令行还是
# Qt Creator 构建，各自那个构建目录都会被正确部署。
if(WIN32)
    get_filename_component(_sora_qt_bin "${Qt6_DIR}/../../../bin" ABSOLUTE)
    find_program(SORA_WINDEPLOYQT windeployqt HINTS "${_sora_qt_bin}")
    if(SORA_WINDEPLOYQT)
        add_custom_command(TARGET Sora POST_BUILD
            COMMAND "${SORA_WINDEPLOYQT}" --no-translations "$<TARGET_FILE:Sora>"
            COMMENT "windeployqt: 部署 Qt 运行库到 $<TARGET_FILE_DIR:Sora>"
            VERBATIM
        )
    else()
        message(WARNING "未找到 windeployqt，双击 Sora.exe 可能因缺少 Qt 运行库而无法启动")
    endif()
endif()

install(TARGETS Sora
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET Sora
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

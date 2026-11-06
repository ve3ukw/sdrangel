project(jogwheel)

set(jogwheel_SOURCES
    jogwheel.cpp
    jogwheelsettings.cpp
    jogwheelplugin.cpp
    jogwheelwebapiadapter.cpp
)

set(jogwheel_HEADERS
    jogwheel.h
    jogwheelsettings.h
    jogwheelplugin.h
    jogwheelwebapiadapter.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/swagger/sdrangel/code/qt5/client
)

if(NOT SERVER_MODE)
    set(jogwheel_SOURCES
        ${jogwheel_SOURCES}
        jogwheelgui.cpp
        jogwheelgui.ui
        jogwheelknob.cpp
    )
    set(jogwheel_HEADERS
        ${jogwheel_HEADERS}
        jogwheelgui.h
        jogwheelknob.h
    )

    set(TARGET_NAME ${PLUGINS_PREFIX}featurejogwheel)
    set(TARGET_LIB "Qt::Widgets" Qt::Network)
    set(TARGET_LIB_GUI "sdrgui")
    set(INSTALL_FOLDER ${INSTALL_PLUGINS_DIR})
else()
    set(TARGET_NAME ${PLUGINSSRV_PREFIX}featurejogwheelsrv)
    set(TARGET_LIB Qt::Network)
    set(TARGET_LIB_GUI "")
    set(INSTALL_FOLDER ${INSTALL_PLUGINSSRV_DIR})
endif()

if(NOT Qt6_FOUND)
    add_library(${TARGET_NAME} ${jogwheel_SOURCES})
else()
    qt_add_plugin(${TARGET_NAME} CLASS_NAME JogWheelPlugin)
    target_sources(${TARGET_NAME} PRIVATE ${jogwheel_SOURCES})
endif()

if(NOT BUILD_SHARED_LIBS)
    set_property(GLOBAL APPEND PROPERTY STATIC_PLUGINS_PROPERTY ${TARGET_NAME})
endif()

target_link_libraries(${TARGET_NAME} PRIVATE
    Qt::Core
    ${TARGET_LIB}
    sdrbase
    ${TARGET_LIB_GUI}
)

install(TARGETS ${TARGET_NAME} DESTINATION ${INSTALL_FOLDER})

if (WIN32)
    install(FILES $<TARGET_PDB_FILE:${TARGET_NAME}> CONFIGURATIONS Debug RelWithDebInfo DESTINATION ${INSTALL_FOLDER})
endif()

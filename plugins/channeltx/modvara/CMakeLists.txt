project(modvara)

set(modvara_SOURCES
    varamodsettings.cpp
    varamodsource.cpp
    varamodbaseband.cpp
    varamod.cpp
    varamodplugin.cpp
    varamodwebapiadapter.cpp
)

set(modvara_HEADERS
    varamodsettings.h
    varamodsource.h
    varamodbaseband.h
    varamod.h
    varamodplugin.h
    varamodwebapiadapter.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/swagger/sdrangel/code/qt5/client
    ${CMAKE_SOURCE_DIR}/modemvara
)

if(NOT SERVER_MODE)
    list(APPEND modvara_SOURCES
        varamodgui.cpp
        varamodgui.ui
    )
    list(APPEND modvara_HEADERS varamodgui.h)
    set(TARGET_NAME ${PLUGINS_PREFIX}modvara)
    set(TARGET_LIB_GUI "sdrgui")
    set(INSTALL_FOLDER ${INSTALL_PLUGINS_DIR})
else()
    set(TARGET_NAME ${PLUGINSSRV_PREFIX}modvarasrv)
    set(TARGET_LIB_GUI "")
    set(INSTALL_FOLDER ${INSTALL_PLUGINSSRV_DIR})
endif()

if(NOT Qt6_FOUND)
    add_library(${TARGET_NAME} ${modvara_SOURCES})
else()
    qt_add_plugin(${TARGET_NAME} CLASS_NAME VARAModPlugin)
    target_sources(${TARGET_NAME} PRIVATE ${modvara_SOURCES})
endif()

if(NOT BUILD_SHARED_LIBS)
    set_property(GLOBAL APPEND PROPERTY STATIC_PLUGINS_PROPERTY ${TARGET_NAME})
endif()

target_link_libraries(${TARGET_NAME} PRIVATE
    Qt::Core
    Qt::Gui
    sdrbase
    ${TARGET_LIB_GUI}
    modemvara
)

install(TARGETS ${TARGET_NAME} DESTINATION ${INSTALL_FOLDER})

if(WIN32)
    install(FILES $<TARGET_PROPERTY:${TARGET_NAME},RUNTIME_OUTPUT_DIRECTORY>/${TARGET_NAME}stripped.pdb CONFIGURATIONS Release DESTINATION ${INSTALL_FOLDER} RENAME ${TARGET_NAME}.pdb)
    install(FILES $<TARGET_PDB_FILE:${TARGET_NAME}> CONFIGURATIONS Debug RelWithDebInfo DESTINATION ${INSTALL_FOLDER})
endif()

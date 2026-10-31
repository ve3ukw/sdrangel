project(demodvara)

set(demodvara_SOURCES
    varademodsettings.cpp
    varademodsink.cpp
    varademodbaseband.cpp
    varademod.cpp
    varademodplugin.cpp
    varademodwebapiadapter.cpp
)

set(demodvara_HEADERS
    varademodsettings.h
    varademodsink.h
    varademodbaseband.h
    varademod.h
    varademodplugin.h
    varademodwebapiadapter.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/swagger/sdrangel/code/qt5/client
    ${CMAKE_SOURCE_DIR}/modemvara
)

if(NOT SERVER_MODE)
    list(APPEND demodvara_SOURCES
        varademodgui.cpp
        varademodgui.ui
    )
    list(APPEND demodvara_HEADERS varademodgui.h)
    set(TARGET_NAME ${PLUGINS_PREFIX}demodvara)
    set(TARGET_LIB_GUI "sdrgui")
    set(INSTALL_FOLDER ${INSTALL_PLUGINS_DIR})
else()
    set(TARGET_NAME ${PLUGINSSRV_PREFIX}demodvarasrv)
    set(TARGET_LIB_GUI "")
    set(INSTALL_FOLDER ${INSTALL_PLUGINSSRV_DIR})
endif()

if(NOT Qt6_FOUND)
    add_library(${TARGET_NAME} ${demodvara_SOURCES})
else()
    qt_add_plugin(${TARGET_NAME} CLASS_NAME VARADemodPlugin)
    target_sources(${TARGET_NAME} PRIVATE ${demodvara_SOURCES})
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

project(demodsstv)

set(demodsstv_SOURCES
    sstvmodes.cpp
    sstvdecoder.cpp
    sstvdemodsettings.cpp
    sstvdemodreport.cpp
    sstvdemodsink.cpp
    sstvdemodbaseband.cpp
    sstvdemod.cpp
    sstvdemodplugin.cpp
    sstvdemodwebapiadapter.cpp
)

set(demodsstv_HEADERS
    sstvmodes.h
    sstvdecoder.h
    sstvdemodsettings.h
    sstvdemodreport.h
    sstvdemodsink.h
    sstvdemodbaseband.h
    sstvdemod.h
    sstvdemodplugin.h
    sstvdemodwebapiadapter.h
)

include_directories(${CMAKE_SOURCE_DIR}/swagger/sdrangel/code/qt5/client)

if(NOT SERVER_MODE)
    list(APPEND demodsstv_SOURCES
        sstvdemodgui.cpp
        sstvdemodgui.ui
    )
    list(APPEND demodsstv_HEADERS sstvdemodgui.h)
    set(TARGET_NAME ${PLUGINS_PREFIX}demodsstv)
    set(TARGET_LIB_GUI "sdrgui")
    set(INSTALL_FOLDER ${INSTALL_PLUGINS_DIR})
else()
    set(TARGET_NAME ${PLUGINSSRV_PREFIX}demodsstvsrv)
    set(TARGET_LIB_GUI "")
    set(INSTALL_FOLDER ${INSTALL_PLUGINSSRV_DIR})
endif()

if(NOT Qt6_FOUND)
    add_library(${TARGET_NAME} ${demodsstv_SOURCES})
else()
    qt_add_plugin(${TARGET_NAME} CLASS_NAME SSTVDemodPlugin)
    target_sources(${TARGET_NAME} PRIVATE ${demodsstv_SOURCES})
endif()

if(NOT BUILD_SHARED_LIBS)
    set_property(GLOBAL APPEND PROPERTY STATIC_PLUGINS_PROPERTY ${TARGET_NAME})
endif()

target_link_libraries(${TARGET_NAME} PRIVATE
    Qt::Core
    Qt::Gui
    sdrbase
    ${TARGET_LIB_GUI}
)

install(TARGETS ${TARGET_NAME} DESTINATION ${INSTALL_FOLDER})

if(WIN32)
    install(FILES $<TARGET_PROPERTY:${TARGET_NAME},RUNTIME_OUTPUT_DIRECTORY>/${TARGET_NAME}stripped.pdb CONFIGURATIONS Release DESTINATION ${INSTALL_FOLDER} RENAME ${TARGET_NAME}.pdb)
    install(FILES $<TARGET_PDB_FILE:${TARGET_NAME}> CONFIGURATIONS Debug RelWithDebInfo DESTINATION ${INSTALL_FOLDER})
endif()

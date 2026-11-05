project(demodssdv)

set(demodssdv_SOURCES
    ssdvbpskdemod.cpp
    ssdvdemodframeparser.cpp
    ssdvdemodsettings.cpp
    ssdvdemodreport.cpp
    ssdvdemodsink.cpp
    ssdvdemodbaseband.cpp
    ssdvdemod.cpp
    ssdvdemodplugin.cpp
    ssdvdemodwebapiadapter.cpp
)

set(demodssdv_HEADERS
    ssdvbpskdemod.h
    ssdvdemodframeparser.h
    ssdvdemodsettings.h
    ssdvdemodreport.h
    ssdvdemodsink.h
    ssdvdemodbaseband.h
    ssdvdemod.h
    ssdvdemodplugin.h
    ssdvdemodwebapiadapter.h
)

include_directories(${CMAKE_SOURCE_DIR}/swagger/sdrangel/code/qt5/client)

if(NOT SERVER_MODE)
    list(APPEND demodssdv_SOURCES
        ssdvdemodgui.cpp
        ssdvdemodgui.ui
    )
    list(APPEND demodssdv_HEADERS ssdvdemodgui.h)
    set(TARGET_NAME ${PLUGINS_PREFIX}demodssdv)
    set(TARGET_LIB_GUI "sdrgui")
    set(INSTALL_FOLDER ${INSTALL_PLUGINS_DIR})
else()
    set(TARGET_NAME ${PLUGINSSRV_PREFIX}demodssdvsrv)
    set(TARGET_LIB_GUI "")
    set(INSTALL_FOLDER ${INSTALL_PLUGINSSRV_DIR})
endif()

if(NOT Qt6_FOUND)
    add_library(${TARGET_NAME} ${demodssdv_SOURCES})
else()
    qt_add_plugin(${TARGET_NAME} CLASS_NAME SSDVDemodPlugin)
    target_sources(${TARGET_NAME} PRIVATE ${demodssdv_SOURCES})
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

project(demodssdv)

set(demodssdv_SOURCES
    ssdvbpskdemod.cpp
    ssdvfskdemod.cpp
    ssdvdemoddeframers.cpp
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
    ssdvfskdemod.h
    ssdvdemoddeframers.h
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

# Loopback of the SSDV Mod source into the SSDV Demod sink
option(SSDV_BUILD_TESTS "Build the SSDV loopback test" OFF)
if(SSDV_BUILD_TESTS AND NOT SERVER_MODE)
    set(MODSSDV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../channeltx/modssdv)
    add_executable(ssdv_loopback
        tests/test_loopback.cpp
        ssdvbpskdemod.cpp
        ssdvfskdemod.cpp
        ssdvdemoddeframers.cpp
        ssdvdemodframeparser.cpp
        ssdvdemodsettings.cpp
        ssdvdemodreport.cpp
        ssdvdemodsink.cpp
        ${MODSSDV_DIR}/ssdvmodsettings.cpp
        ${MODSSDV_DIR}/ssdvmodreport.cpp
        ${MODSSDV_DIR}/ssdvmodframer.cpp
        ${MODSSDV_DIR}/ssdvmodsource.cpp)
    target_include_directories(ssdv_loopback PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${MODSSDV_DIR})
    target_link_libraries(ssdv_loopback PRIVATE Qt::Core Qt::Gui sdrbase)
    # Runs recordings through the demodulator
    add_executable(ssdv_iqfile
        tests/test_iqfile.cpp
        ssdvbpskdemod.cpp
        ssdvfskdemod.cpp
        ssdvdemoddeframers.cpp
        ssdvdemodframeparser.cpp
        ssdvdemodsettings.cpp
        ssdvdemodreport.cpp
        ssdvdemodsink.cpp)
    target_include_directories(ssdv_iqfile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ssdv_iqfile PRIVATE Qt::Core Qt::Gui sdrbase)
    add_test(NAME ssdv_loopback_ao123 COMMAND ssdv_loopback -f 0 -e 6)
    add_test(NAME ssdv_loopback_standard COMMAND ssdv_loopback -f 1 -e 6)
endif()

if(WIN32)
    install(FILES $<TARGET_PROPERTY:${TARGET_NAME},RUNTIME_OUTPUT_DIRECTORY>/${TARGET_NAME}stripped.pdb CONFIGURATIONS Release DESTINATION ${INSTALL_FOLDER} RENAME ${TARGET_NAME}.pdb)
    install(FILES $<TARGET_PDB_FILE:${TARGET_NAME}> CONFIGURATIONS Debug RelWithDebInfo DESTINATION ${INSTALL_FOLDER})
endif()

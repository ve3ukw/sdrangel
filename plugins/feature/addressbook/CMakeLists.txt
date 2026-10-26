project(addressbook)

set(addressbook_SOURCES
    addressbook.cpp
    addressbookentry.cpp
    addressbooksettings.cpp
    addressbookplugin.cpp
)

set(addressbook_HEADERS
    addressbook.h
    addressbookentry.h
    addressbooksettings.h
    addressbookplugin.h
    addressbookwebapiadapter.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/swagger/sdrangel/code/qt5/client
)

if(NOT SERVER_MODE)
    set(addressbook_SOURCES
        ${addressbook_SOURCES}
        addressbookgui.cpp
        addressbookentrydialog.cpp
        addressbookgui.ui
    )
    set(addressbook_HEADERS
        ${addressbook_HEADERS}
        addressbookgui.h
        addressbookentrydialog.h
    )

    set(TARGET_NAME ${PLUGINS_PREFIX}featureaddressbook)
    set(TARGET_LIB "Qt::Widgets")
    set(TARGET_LIB_GUI "sdrgui")
    set(INSTALL_FOLDER ${INSTALL_PLUGINS_DIR})
else()
    set(TARGET_NAME ${PLUGINSSRV_PREFIX}featureaddressbooksrv)
    set(TARGET_LIB "")
    set(TARGET_LIB_GUI "")
    set(INSTALL_FOLDER ${INSTALL_PLUGINSSRV_DIR})
endif()

if(NOT Qt6_FOUND)
    add_library(${TARGET_NAME} ${addressbook_SOURCES})
else()
    qt_add_plugin(${TARGET_NAME} CLASS_NAME AddressBookPlugin)
    target_sources(${TARGET_NAME} PRIVATE ${addressbook_SOURCES})
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

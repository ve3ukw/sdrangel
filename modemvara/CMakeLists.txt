project(modemvara)

set(modemvara_SOURCES
    varaengine.cpp
    varatxlink.cpp
)

set(modemvara_HEADERS
    varaengine.h
    varatxlink.h
)

include_directories(
    ${CMAKE_SOURCE_DIR}/exports
)

add_library(modemvara
	${modemvara_SOURCES}
)

target_link_libraries(modemvara
    Qt::Core
    xara_ffi
)

install(TARGETS modemvara DESTINATION ${INSTALL_LIB_DIR})

# Two VARA stations back to back through the VARA Demod/Mod DSP, with no radio. Not built by default:
#   make varaloopback && bin/varaloopback
if (ENABLE_CHANNELRX_DEMODVARA AND ENABLE_CHANNELTX_MODVARA)
    add_executable(varaloopback EXCLUDE_FROM_ALL
        test/varaloopback.cpp
        ${CMAKE_SOURCE_DIR}/plugins/channelrx/demodvara/varademodsink.cpp
        ${CMAKE_SOURCE_DIR}/plugins/channelrx/demodvara/varademodsettings.cpp
        ${CMAKE_SOURCE_DIR}/plugins/channeltx/modvara/varamodsource.cpp
        ${CMAKE_SOURCE_DIR}/plugins/channeltx/modvara/varamodsettings.cpp
    )
    target_include_directories(varaloopback PRIVATE
        ${CMAKE_SOURCE_DIR}/sdrbase
        ${CMAKE_SOURCE_DIR}/modemvara
        ${CMAKE_SOURCE_DIR}/plugins/channelrx/demodvara
        ${CMAKE_SOURCE_DIR}/plugins/channeltx/modvara
    )
    target_link_libraries(varaloopback modemvara sdrbase Qt::Core)
endif()

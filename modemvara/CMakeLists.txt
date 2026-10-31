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

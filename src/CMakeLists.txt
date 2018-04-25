find_package(PkgConfig)

pkg_check_modules (
    LIBCURL REQUIRED libcurl
    )

add_executable (
    kellersensoren
    kellersensoren.cpp
    )

target_link_libraries (
    kellersensoren 
    LINK_PUBLIC wiringPi wiringPiDev ${LIBCURL_LIBRARIES}
    )

target_include_directories (
    kellersensoren 
    PUBLIC ${LIBCURL_INCLUDE_DIRS}
    )

target_compile_options (
    kellersensoren 
    PUBLIC "-std=c++11" ${LIBCURL_CXXFLAGS_OTHER}
    )

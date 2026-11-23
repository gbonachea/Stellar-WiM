cmake_minimum_required(VERSION 3.16)
project(stellar-wim-settings VERSION 0.8.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
add_executable(stellar-wim-settings
  src/main.cpp src/stellaripcclient.cpp src/stellaripcclient.h
  src/stellarwimconfigwidget.cpp src/stellarwimconfigwidget.h)
target_link_libraries(stellar-wim-settings PRIVATE Qt6::Widgets Qt6::Network)
include(GNUInstallDirs)
install(TARGETS stellar-wim-settings RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/stellar-wim)
configure_file(stellar-wim-settings.desktop stellar-wim-config.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stellar-wim-config.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME stellar-wim-settings)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_DEBIAN_PACKAGE_RELEASE 1)
set(CPACK_PACKAGE_CONTACT "Stellar WiM Maintainers <stellar-wim@example.invalid>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Standalone settings application for Stellar WiM")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
include(CPack)

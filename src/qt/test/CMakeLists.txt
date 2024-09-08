# Copyright (c) 2024-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_executable(test_xaya-qt
  apptests.cpp
  optiontests.cpp
  rpcnestedtests.cpp
  test_main.cpp
  uritests.cpp
  util.cpp
  ../../init/bitcoin-qt.cpp
)

target_link_libraries(test_xaya-qt
  core_interface
  bitcoinqt
  test_util
  bitcoin_node
  Boost::headers
  Qt5::Test
)

import_plugins(test_xaya-qt)

if(ENABLE_WALLET)
  target_sources(test_xaya-qt
    PRIVATE
      addressbooktests.cpp
      wallettests.cpp
      ../../wallet/test/wallet_test_fixture.cpp
  )
endif()

if(NOT QT_IS_STATIC)
  add_custom_command(
    TARGET test_xaya-qt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_PROPERTY:Qt5::QMinimalIntegrationPlugin,LOCATION_$<UPPER_CASE:$<CONFIG>>> $<TARGET_FILE_DIR:test_xaya-qt>/plugins/platforms
    VERBATIM
  )
endif()

add_test(NAME test_xaya-qt
  COMMAND test_xaya-qt
)
if(WIN32 AND VCPKG_TARGET_TRIPLET)
  # On Windows, vcpkg configures Qt with `-opengl dynamic`, which makes
  # the "minimal" platform plugin unusable due to internal Qt bugs.
  set_tests_properties(test_xaya-qt PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=windows"
  )
endif()

install(TARGETS test_xaya-qt
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

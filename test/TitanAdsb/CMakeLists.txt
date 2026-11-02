# ============================================================================
# Titan ADS-B Unit Tests
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE AdsbTrafficTest.cc AdsbTrafficTest.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(AdsbTrafficTest LABELS Unit)

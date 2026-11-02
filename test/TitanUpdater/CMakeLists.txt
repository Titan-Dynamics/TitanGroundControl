# ============================================================================
# Titan Updater Unit Tests
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE TitanUpdaterTest.cc TitanUpdaterTest.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(TitanUpdaterTest LABELS Unit)

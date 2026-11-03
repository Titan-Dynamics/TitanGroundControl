# ============================================================================
# Titan Link Stats Unit Tests
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE LinkStatsTest.cc LinkStatsTest.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(LinkStatsTest LABELS Unit)

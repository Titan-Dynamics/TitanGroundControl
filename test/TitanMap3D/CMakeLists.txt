# ============================================================================
# Titan 3D Map Unit Tests
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE Map3DTest.cc Map3DTest.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(Map3DTest LABELS Unit)

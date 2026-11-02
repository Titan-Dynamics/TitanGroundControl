# ============================================================================
# Titan HUD Unit Tests
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE HudPainterTest.cc HudPainterTest.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(HudPainterTest LABELS Unit)

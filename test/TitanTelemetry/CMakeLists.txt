# ============================================================================
# Titan Telemetry Panel Unit Tests
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE TelemetryPanelTest.cc TelemetryPanelTest.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(TelemetryPanelTest LABELS Unit)

# ============================================================================
# Titan Telemetry Module
# The Fly View telemetry panel: tiles of telemetry, each a number or a bar,
# edited in place. The C++ types register in the app's QML module.
# ============================================================================

target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE TelemetryPanelController.cc
            TelemetryPanelController.h
            TelemetryPanelItem.cc
            TelemetryPanelItem.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_library(TitanTelemetryModule STATIC)

qt_add_qml_module(
    TitanTelemetryModule
    URI QGroundControl.TitanTelemetry
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES TelemetryItemEditor.qml TelemetryPanel.qml TelemetryTile.qml
    NO_PLUGIN
)

target_link_libraries(TitanTelemetryModule PUBLIC Qt6::Core Qt6::Qml Qt6::Quick)

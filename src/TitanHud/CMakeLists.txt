# ============================================================================
# Titan HUD Module
# Mission Planner style HUD (QPainter) usable as an instrument, a video overlay
# and a 3D map overlay. The C++ types register in the app's QML module so the
# HUD data source can also feed the 3D map's indicator lines.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE HudData.cc HudData.h HudItem.cc HudItem.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_library(TitanHudModule STATIC)

qt_add_qml_module(
    TitanHudModule
    URI QGroundControl.TitanHud
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES TitanHud.qml TitanHudInstrument.qml
    NO_PLUGIN
)

target_link_libraries(TitanHudModule PUBLIC Qt6::Core Qt6::Qml Qt6::Quick)

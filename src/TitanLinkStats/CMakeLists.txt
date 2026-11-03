# ============================================================================
# Titan Link Stats Module
# The Analyze view's Link Stats page, TitanPlanner's Link Stats: traffic both
# ways on the vehicle's link and a chart of its bandwidth. The C++ types
# register in the app's QML module.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        LinkStats.cc
        LinkStats.h
        LinkStatsController.cc
        LinkStatsController.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_library(TitanLinkStatsModule STATIC)

qt_add_qml_module(
    TitanLinkStatsModule
    URI QGroundControl.TitanLinkStats
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES LinkStatsPage.qml
    NO_PLUGIN
)

target_link_libraries(TitanLinkStatsModule PUBLIC Qt6::Core Qt6::Qml Qt6::Quick)

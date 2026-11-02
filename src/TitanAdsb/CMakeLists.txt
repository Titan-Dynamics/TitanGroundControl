# ============================================================================
# Titan ADS-B Module
# Traffic from the internet (a port of TitanPlanner's adsb.cs), traffic sent
# to the autopilot for avoidance, and the Fly View map's traffic icons. The C++
# types compile into the app target beside upstream's src/ADSB.
# ============================================================================

target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE AdsbCategory.cc
            AdsbCategory.h
            AdsbInternetSource.cc
            AdsbInternetSource.h
            AdsbTrafficSender.cc
            AdsbTrafficSender.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_library(TitanAdsbModule STATIC)

qt_add_qml_module(
    TitanAdsbModule
    URI QGroundControl.TitanAdsb
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES TitanAdsbIcon.qml TitanAdsbMapItem.qml
    NO_PLUGIN
)

target_link_libraries(TitanAdsbModule PUBLIC Qt6::Core Qt6::Qml Qt6::Quick)

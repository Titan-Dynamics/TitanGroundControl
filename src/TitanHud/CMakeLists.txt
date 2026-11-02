# ============================================================================
# Titan HUD Module
# Fighter-style HUD painted with QPainter, used as an instrument panel and a
# video overlay, and the old-television static shown when there is no video.
# The C++ types register in the app's QML module so the HUD data source can
# also feed the 3D map's indicator lines.
# ============================================================================

target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE HudData.cc
            HudData.h
            HudItem.cc
            HudItem.h
            HudPainter.cc
            HudPainter.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# B612 Mono, the HUD's typeface (SIL Open Font License, fonts/OFL.txt). The app target is defined elsewhere, so the
# path is absolute; BASE keeps the resource name short.
qt_add_resources(
    ${CMAKE_PROJECT_NAME}
    "titanhud_fonts"
    PREFIX
    "/fonts"
    BASE
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts
    FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts/B612Mono-Bold.ttf
)

find_package(Qt6 REQUIRED COMPONENTS ShaderTools)

# The app target is defined elsewhere, so the shader path is absolute; BASE keeps the resource name short
qt_add_shaders(
    ${CMAKE_PROJECT_NAME}
    "titanhud_shaders"
    PREFIX
    "/titanhud"
    BASE
    ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tvstatic.frag
)

qt_add_library(TitanHudModule STATIC)

qt_add_qml_module(
    TitanHudModule
    URI QGroundControl.TitanHud
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES TitanHud.qml TitanHudInstrument.qml TvStatic.qml
    NO_PLUGIN
)

target_link_libraries(TitanHudModule PUBLIC Qt6::Core Qt6::Qml Qt6::Quick)

# ============================================================================
# Titan 3D Map Module
# Mission Planner's 3D map (TitanPlanner Map3D) on Qt's RHI: streamed satellite terrain from the shared tile cache,
# chase / FPV / free-look camera, vehicle model, mission, ADS-B and clouds. The C++ types register in the app's QML
# module.
# ============================================================================

find_package(Qt6 REQUIRED COMPONENTS ShaderTools)

target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE Map3DClouds.cc
            Map3DClouds.h
            Map3DGeo.cc
            Map3DGeo.h
            Map3DItem.cc
            Map3DItem.h
            Map3DModel.cc
            Map3DModel.h
            Map3DOverlays.cc
            Map3DOverlays.h
            Map3DRenderer.cc
            Map3DRenderer.h
            Map3DScene.cc
            Map3DScene.h
            Map3DTerrain.cc
            Map3DTerrain.h
            Map3DTileLoader.cc
            Map3DTileLoader.h
            Map3DTypes.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The app target is defined elsewhere, so the shader paths are absolute; BASE keeps the resource names short
qt_add_shaders(
    ${CMAKE_PROJECT_NAME}
    "titanmap3d_shaders"
    PREFIX
    "/titanmap3d"
    BASE
    ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloud.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloud.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/color.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/line.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/model.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/quad.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/quad.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain.vert
)

qt_add_library(TitanMap3DModule STATIC)

set_source_files_properties(Map3DState.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(
    TitanMap3DModule
    URI QGroundControl.TitanMap3D
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES Map3DSettingsAction.qml Map3DShowAction.qml Map3DState.qml Map3DStatsPanel.qml Map3DView.qml
    RESOURCES images/wp_3d.png
              images/wp_3d_alt.png
              models/boat.stl
              models/copter.stl
              models/helicopter.stl
              models/plane.stl
              models/quadplane.stl
              models/rover.stl
              models/sub.stl
    NO_PLUGIN
)

target_link_libraries(TitanMap3DModule PUBLIC Qt6::Core Qt6::Qml Qt6::Quick)

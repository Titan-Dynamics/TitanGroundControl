# ============================================================================
# Titan Updater Module
# Updates from the GitHub releases, stable or development, and the Help
# page's source code and release links. The C++ type registers in the app's
# QML module.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE TitanUpdater.cc TitanUpdater.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_library(TitanUpdaterModule STATIC)

qt_add_qml_module(
    TitanUpdaterModule
    URI QGroundControl.TitanUpdater
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES TitanHelpSection.qml
    NO_PLUGIN
)

target_link_libraries(TitanUpdaterModule PUBLIC Qt6::Core Qt6::Qml Qt6::Quick)

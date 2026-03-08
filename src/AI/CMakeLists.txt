# ============================================================================
# AI Module
# AI-powered chat and command processing for vehicle control
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        AIChatController.cc
        AIChatController.h
        AISystemPrompt.h
        POIItem.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

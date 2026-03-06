#include "AISettings.h"

DECLARE_SETTINGGROUP(AI, "AI")
{
}

DECLARE_SETTINGSFACT(AISettings, claudeApiKey)
DECLARE_SETTINGSFACT(AISettings, groqApiKey)
DECLARE_SETTINGSFACT(AISettings, enableTextToSpeech)
DECLARE_SETTINGSFACT(AISettings, confirmDangerousCommands)
DECLARE_SETTINGSFACT(AISettings, includeVehicleState)
DECLARE_SETTINGSFACT(AISettings, customContext)

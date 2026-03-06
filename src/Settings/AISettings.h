#pragma once

#include <QtQmlIntegration/QtQmlIntegration>

#include "SettingsGroup.h"

class AISettings : public SettingsGroup
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
public:
    AISettings(QObject* parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(claudeApiKey)
    DEFINE_SETTINGFACT(groqApiKey)
    DEFINE_SETTINGFACT(enableTextToSpeech)
    DEFINE_SETTINGFACT(confirmDangerousCommands)
    DEFINE_SETTINGFACT(includeVehicleState)
};

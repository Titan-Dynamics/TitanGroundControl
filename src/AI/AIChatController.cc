#include "AIChatController.h"

#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "AISettings.h"
#include "Vehicle.h"
#include "QGCNetworkHelper.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtPositioning/QGeoCoordinate>

QGC_LOGGING_CATEGORY(AIChatControllerLog, "AI.ChatController")

// Message object for QML model
class AIChatMessage : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int role READ role CONSTANT)
    Q_PROPERTY(QString content READ content NOTIFY contentChanged)
    Q_PROPERTY(QString action READ action CONSTANT)
    Q_PROPERTY(QVariantMap parameters READ parameters CONSTANT)
    Q_PROPERTY(bool requiresConfirmation READ requiresConfirmation CONSTANT)
    Q_PROPERTY(int commandStatus READ commandStatus NOTIFY commandStatusChanged)

public:
    AIChatMessage(AIChatController::MessageRole role, const QString& content,
                  const QString& action = QString(), const QVariantMap& parameters = QVariantMap(),
                  bool requiresConfirmation = false, QObject* parent = nullptr)
        : QObject(parent)
        , _role(role)
        , _content(content)
        , _action(action)
        , _parameters(parameters)
        , _requiresConfirmation(requiresConfirmation)
        , _commandStatus(action.isEmpty() ? AIChatController::CommandStatus::None
                         : (requiresConfirmation ? AIChatController::CommandStatus::RequiresConfirmation
                                                 : AIChatController::CommandStatus::Pending))
    {}

    int role() const { return static_cast<int>(_role); }
    QString content() const { return _content; }
    QString action() const { return _action; }
    QVariantMap parameters() const { return _parameters; }
    bool requiresConfirmation() const { return _requiresConfirmation; }
    int commandStatus() const { return static_cast<int>(_commandStatus); }

    void setContent(const QString& content) { _content = content; emit contentChanged(); }
    void setCommandStatus(AIChatController::CommandStatus status) { _commandStatus = status; emit commandStatusChanged(); }

signals:
    void contentChanged();
    void commandStatusChanged();

private:
    AIChatController::MessageRole _role;
    QString _content;
    QString _action;
    QVariantMap _parameters;
    bool _requiresConfirmation;
    AIChatController::CommandStatus _commandStatus;
};

AIChatController::AIChatController(Vehicle* vehicle, QObject* parent)
    : QObject(parent)
    , _vehicle(vehicle)
    , _networkManager(QGCNetworkHelper::createNetworkManager(this))
    , _messages(new QmlObjectListModel(this))
{
    qCDebug(AIChatControllerLog) << "Created for vehicle:" << (vehicle ? vehicle->id() : -1);
}

AIChatController::~AIChatController()
{
    if (_pendingReply) {
        _pendingReply->abort();
    }
    qCDebug(AIChatControllerLog) << "Destroyed";
}

void AIChatController::sendMessage(const QString& userMessage)
{
    if (userMessage.trimmed().isEmpty()) {
        return;
    }

    if (_isProcessing) {
        qCWarning(AIChatControllerLog) << "Already processing a request";
        return;
    }

    // Add user message to chat
    _addMessage(MessageRole::User, userMessage);

    // Send to Claude API
    _sendToClaudeAPI(userMessage);
}

void AIChatController::executeCommand(int messageIndex)
{
    if (messageIndex < 0 || messageIndex >= _messages->count()) {
        qCWarning(AIChatControllerLog) << "Invalid message index:" << messageIndex;
        return;
    }

    auto* message = qobject_cast<AIChatMessage*>(_messages->get(messageIndex));
    if (!message || message->action().isEmpty()) {
        qCWarning(AIChatControllerLog) << "No command to execute at index:" << messageIndex;
        return;
    }

    bool success = _executeVehicleCommand(message->action(), message->parameters());
    message->setCommandStatus(success ? CommandStatus::Executed : CommandStatus::Failed);
}

void AIChatController::clearHistory()
{
    _messages->clearAndDeleteContents();
    _errorMessage.clear();
    emit errorMessageChanged();
}

void AIChatController::_sendToClaudeAPI(const QString& userMessage)
{
    auto* aiSettings = SettingsManager::instance()->aiSettings();
    QString apiKey = aiSettings->claudeApiKey()->rawValue().toString();

    if (apiKey.isEmpty()) {
        _errorMessage = tr("Claude API key not configured. Please set it in Titan AI settings.");
        emit errorMessageChanged();
        _addMessage(MessageRole::System, _errorMessage);
        return;
    }

    _isProcessing = true;
    emit isProcessingChanged();

    // Build the request
    QUrl url("https://api.anthropic.com/v1/messages");
    QNetworkRequest request = QGCNetworkHelper::createRequest(url);
    QGCNetworkHelper::setJsonHeaders(request);
    request.setRawHeader("x-api-key", apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");

    // Build message history for context
    QJsonArray messagesArray;

    // Add conversation history (last N messages, excluding the one we just added)
    int historyStart = qMax(0, _messages->count() - 11);
    for (int i = historyStart; i < _messages->count(); ++i) {
        auto* msg = qobject_cast<AIChatMessage*>(_messages->get(i));
        if (msg && msg->role() != static_cast<int>(MessageRole::System)) {
            QJsonObject msgObj;
            msgObj["role"] = (msg->role() == static_cast<int>(MessageRole::User)) ? "user" : "assistant";
            msgObj["content"] = msg->content();
            messagesArray.append(msgObj);
        }
    }

    // Ensure we have at least one message (the user's message we just added should be included)
    if (messagesArray.isEmpty()) {
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = userMessage;
        messagesArray.append(userMsg);
    }

    // Build request body
    QJsonObject requestBody;
    requestBody["model"] = "claude-sonnet-4-5-20250929";
    requestBody["max_tokens"] = 1024;
    requestBody["system"] = _buildSystemPrompt();
    requestBody["messages"] = messagesArray;

    QByteArray requestData = QJsonDocument(requestBody).toJson();
    qCDebug(AIChatControllerLog) << "Sending request to Claude API";

    _pendingReply = _networkManager->post(request, requestData);
    connect(_pendingReply, &QNetworkReply::finished, this, &AIChatController::_onNetworkReplyFinished);
}

QString AIChatController::_buildSystemPrompt() const
{
    auto* aiSettings = SettingsManager::instance()->aiSettings();
    bool includeState = aiSettings->includeVehicleState()->rawValue().toBool();

    QString prompt = R"(You are an AI assistant for drone/vehicle control in a ground control station.
You can issue commands to control the vehicle. Always respond with a JSON object.

AVAILABLE COMMANDS:
- arm: Arm the vehicle motors. Parameters: none
- disarm: Disarm the vehicle motors (only when not flying). Parameters: none
- takeoff: Take off to specified altitude. Parameters: altitude_m (number, required)
- land: Land at current position. Parameters: none
- rtl: Return to launch/home position. Parameters: smart_rtl (boolean, optional, default false)
- goto: Go to specified location. Parameters: latitude (number), longitude (number), altitude_m (number, optional)
- pause: Pause/hold current position. Parameters: none
- change_altitude: Change altitude relative to current. Parameters: altitude_change_m (number, can be negative)
- emergency_stop: EMERGENCY - Kill all motors immediately. Parameters: none
- set_flight_mode: Change flight mode. Parameters: mode_name (string)

RESPONSE FORMAT (always respond with valid JSON):
{
    "understood": true,
    "action": "command_name_or_null",
    "parameters": {},
    "message": "Human-readable response to user",
    "confirmation_needed": false
}

RULES:
- If you cannot understand the request or it's just a question, set action to null
- Always set confirmation_needed=true for: arm, takeoff, emergency_stop, disarm
- Never execute disarm while the vehicle is flying
- Validate altitude requests are reasonable (typically 2-400m)
- If unsure about the user's intent, ask for clarification in the message field
- Be concise but helpful in your message responses)";

    if (includeState && _vehicle) {
        prompt += "\n\nCURRENT VEHICLE STATE:\n" + _getVehicleStateContext();
    }

    return prompt;
}

QString AIChatController::_getVehicleStateContext() const
{
    if (!_vehicle) {
        return "No vehicle connected";
    }

    QString state;
    state += QString("- Armed: %1\n").arg(_vehicle->armed() ? "Yes" : "No");
    state += QString("- Flying: %1\n").arg(_vehicle->flying() ? "Yes" : "No");
    state += QString("- Flight Mode: %1\n").arg(_vehicle->flightMode());

    if (_vehicle->altitudeRelative()) {
        state += QString("- Altitude (relative): %1 m\n").arg(_vehicle->altitudeRelative()->rawValue().toDouble(), 0, 'f', 1);
    }
    if (_vehicle->groundSpeed()) {
        state += QString("- Ground Speed: %1 m/s\n").arg(_vehicle->groundSpeed()->rawValue().toDouble(), 0, 'f', 1);
    }

    QGeoCoordinate coord = _vehicle->coordinate();
    if (coord.isValid()) {
        state += QString("- Position: %1, %2\n").arg(coord.latitude(), 0, 'f', 6).arg(coord.longitude(), 0, 'f', 6);
    }

    return state;
}

void AIChatController::_onNetworkReplyFinished()
{
    _isProcessing = false;
    emit isProcessingChanged();

    if (!_pendingReply) {
        return;
    }

    QNetworkReply* reply = _pendingReply;
    _pendingReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        qCWarning(AIChatControllerLog) << "API request failed:" << error;

        // Try to get more details from response body
        QByteArray responseData = reply->readAll();
        qCWarning(AIChatControllerLog) << "API response body:" << responseData;

        if (!responseData.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                if (obj.contains("error")) {
                    QJsonObject errorObj = obj["error"].toObject();
                    QString apiError = errorObj["message"].toString();
                    if (!apiError.isEmpty()) {
                        error = apiError;
                    }
                }
            }
        }

        _errorMessage = tr("API Error: %1").arg(error);
        emit errorMessageChanged();

        // Don't crash if adding message fails
        try {
            _addMessage(MessageRole::System, _errorMessage);
        } catch (...) {
            qCWarning(AIChatControllerLog) << "Failed to add error message to chat";
        }
    } else {
        _processAIResponse(reply->readAll());
    }

    reply->deleteLater();
}

void AIChatController::_processAIResponse(const QByteArray& responseData)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(AIChatControllerLog) << "Failed to parse API response:" << parseError.errorString();
        _addMessage(MessageRole::System, tr("Failed to parse AI response"));
        return;
    }

    QJsonObject response = doc.object();

    // Extract content from Claude's response format
    QJsonArray contentArray = response["content"].toArray();
    if (contentArray.isEmpty()) {
        _addMessage(MessageRole::System, tr("Empty response from AI"));
        return;
    }

    QString textContent;
    for (const QJsonValue& item : contentArray) {
        QJsonObject contentObj = item.toObject();
        if (contentObj["type"].toString() == "text") {
            textContent = contentObj["text"].toString();
            break;
        }
    }

    qCDebug(AIChatControllerLog) << "AI response text:" << textContent;

    // Sometimes the AI wraps JSON in markdown code blocks, strip them
    QString cleanedContent = textContent.trimmed();
    if (cleanedContent.startsWith("```json")) {
        cleanedContent = cleanedContent.mid(7);
    } else if (cleanedContent.startsWith("```")) {
        cleanedContent = cleanedContent.mid(3);
    }
    if (cleanedContent.endsWith("```")) {
        cleanedContent.chop(3);
    }
    cleanedContent = cleanedContent.trimmed();

    // Parse the AI's JSON response
    QJsonDocument aiDoc = QJsonDocument::fromJson(cleanedContent.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !aiDoc.isObject()) {
        qCDebug(AIChatControllerLog) << "AI response is not JSON, showing as text. Parse error:" << parseError.errorString();
        // If not valid JSON, just show the text response
        _addMessage(MessageRole::Assistant, textContent);
        return;
    }

    QJsonObject aiResponse = aiDoc.object();
    QString message = aiResponse["message"].toString();
    QString action = aiResponse["action"].toString();
    QVariantMap parameters = aiResponse["parameters"].toObject().toVariantMap();
    bool confirmationNeeded = aiResponse["confirmation_needed"].toBool();

    qCDebug(AIChatControllerLog) << "Parsed - message:" << message << "action:" << action;

    // If message is empty but we have valid JSON, create a default message
    if (message.isEmpty()) {
        if (!action.isEmpty()) {
            message = tr("Command: %1").arg(action);
        } else {
            message = tr("(No response message)");
        }
    }

    // Check if dangerous command requires confirmation
    auto* aiSettings = SettingsManager::instance()->aiSettings();
    bool forceConfirm = aiSettings->confirmDangerousCommands()->rawValue().toBool();

    if (forceConfirm && _isDangerousCommand(action)) {
        confirmationNeeded = true;
    }

    // Add the AI's message
    _addMessage(MessageRole::Assistant, message, action, parameters, confirmationNeeded);

    // Auto-execute if no confirmation needed and we have an action
    if (!action.isEmpty() && !confirmationNeeded) {
        int lastIndex = _messages->count() - 1;
        executeCommand(lastIndex);
    }
}

void AIChatController::_addMessage(MessageRole role, const QString& content, const QString& action,
                                    const QVariantMap& parameters, bool requiresConfirmation)
{
    auto* message = new AIChatMessage(role, content, action, parameters, requiresConfirmation, this);
    _messages->append(message);
}

bool AIChatController::_executeVehicleCommand(const QString& action, const QVariantMap& parameters)
{
    if (!_vehicle) {
        qCWarning(AIChatControllerLog) << "No vehicle to execute command on";
        return false;
    }

    qCDebug(AIChatControllerLog) << "Executing command:" << action << "with params:" << parameters;

    if (action == "arm") {
        _vehicle->setArmed(true, true);
        return true;
    }
    else if (action == "disarm") {
        if (_vehicle->flying()) {
            qCWarning(AIChatControllerLog) << "Cannot disarm while flying";
            return false;
        }
        _vehicle->setArmed(false, true);
        return true;
    }
    else if (action == "takeoff") {
        double altitude = parameters.value("altitude_m", 10.0).toDouble();
        _vehicle->guidedModeTakeoff(altitude);
        return true;
    }
    else if (action == "land") {
        _vehicle->guidedModeLand();
        return true;
    }
    else if (action == "rtl") {
        bool smartRtl = parameters.value("smart_rtl", false).toBool();
        _vehicle->guidedModeRTL(smartRtl);
        return true;
    }
    else if (action == "goto") {
        double lat = parameters.value("latitude").toDouble();
        double lon = parameters.value("longitude").toDouble();
        double alt = parameters.value("altitude_m", -1).toDouble();

        QGeoCoordinate coord(lat, lon);
        if (!coord.isValid()) {
            qCWarning(AIChatControllerLog) << "Invalid coordinates for goto:" << lat << lon;
            return false;
        }

        if (alt > 0) {
            coord.setAltitude(alt);
        }

        _vehicle->guidedModeGotoLocation(coord);
        return true;
    }
    else if (action == "pause") {
        _vehicle->pauseVehicle();
        return true;
    }
    else if (action == "change_altitude") {
        double change = parameters.value("altitude_change_m", 0).toDouble();
        _vehicle->guidedModeChangeAltitude(change, true);
        return true;
    }
    else if (action == "emergency_stop") {
        _vehicle->emergencyStop();
        return true;
    }
    else if (action == "set_flight_mode") {
        QString modeName = parameters.value("mode_name").toString();
        if (!modeName.isEmpty()) {
            _vehicle->setFlightMode(modeName);
            return true;
        }
        return false;
    }

    qCWarning(AIChatControllerLog) << "Unknown action:" << action;
    return false;
}

bool AIChatController::_isDangerousCommand(const QString& action)
{
    static const QStringList dangerousCommands = {
        "arm", "disarm", "takeoff", "emergency_stop"
    };
    return dangerousCommands.contains(action);
}

#include "AIChatController.moc"

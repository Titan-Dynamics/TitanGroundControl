#include "AIChatController.h"

#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "AISettings.h"
#include "AppSettings.h"
#include "AudioOutput.h"
#include "Fact.h"
#include "Vehicle.h"
#include "ParameterManager.h"
#include "QGCNetworkHelper.h"

#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryFile>
#include <QtCore/QTimer>
#include <QtMultimedia/QAudioDevice>
#include <QtMultimedia/QAudioInput>
#include <QtMultimedia/QMediaCaptureSession>
#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QMediaFormat>
#include <QtMultimedia/QMediaRecorder>
#include <QtNetwork/QHttpMultiPart>
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
    Q_PROPERTY(int actionCount READ actionCount CONSTANT)
    Q_PROPERTY(int executedActionCount READ executedActionCount NOTIFY executedActionCountChanged)

public:
    AIChatMessage(AIChatController::MessageRole role, const QString& content,
                  const QString& action = QString(), const QVariantMap& parameters = QVariantMap(),
                  bool requiresConfirmation = false, QObject* parent = nullptr)
        : QObject(parent)
        , _role(role)
        , _content(content)
        , _requiresConfirmation(requiresConfirmation)
        , _commandStatus(action.isEmpty() ? AIChatController::CommandStatus::None
                         : (requiresConfirmation ? AIChatController::CommandStatus::RequiresConfirmation
                                                 : AIChatController::CommandStatus::Pending))
    {
        // Store single action in the actions list
        if (!action.isEmpty()) {
            QVariantMap actionEntry;
            actionEntry["action"] = action;
            actionEntry["parameters"] = parameters;
            _actions.append(actionEntry);
        }
    }

    // Constructor for multiple actions
    AIChatMessage(AIChatController::MessageRole role, const QString& content,
                  const QVariantList& actions, bool requiresConfirmation = false, QObject* parent = nullptr)
        : QObject(parent)
        , _role(role)
        , _content(content)
        , _actions(actions)
        , _requiresConfirmation(requiresConfirmation)
        , _commandStatus(actions.isEmpty() ? AIChatController::CommandStatus::None
                         : (requiresConfirmation ? AIChatController::CommandStatus::RequiresConfirmation
                                                 : AIChatController::CommandStatus::Pending))
    {}

    int role() const { return static_cast<int>(_role); }
    QString content() const { return _content; }

    // For backwards compatibility, return first action name
    QString action() const {
        if (_actions.isEmpty()) return QString();
        QVariantMap first = _actions.first().toMap();
        return first["action"].toString();
    }

    // For backwards compatibility, return first action's parameters
    QVariantMap parameters() const {
        if (_actions.isEmpty()) return QVariantMap();
        QVariantMap first = _actions.first().toMap();
        return first["parameters"].toMap();
    }

    bool requiresConfirmation() const { return _requiresConfirmation; }
    int commandStatus() const { return static_cast<int>(_commandStatus); }
    int actionCount() const { return _actions.count(); }
    int executedActionCount() const { return _executedActionCount; }
    QVariantList actions() const { return _actions; }

    void setContent(const QString& content) { _content = content; emit contentChanged(); }
    void setCommandStatus(AIChatController::CommandStatus status) { _commandStatus = status; emit commandStatusChanged(); }
    void setExecutedActionCount(int count) { _executedActionCount = count; emit executedActionCountChanged(); }

signals:
    void contentChanged();
    void commandStatusChanged();
    void executedActionCountChanged();

private:
    AIChatController::MessageRole _role;
    QString _content;
    QVariantList _actions;  // List of {action: string, parameters: map}
    bool _requiresConfirmation;
    AIChatController::CommandStatus _commandStatus;
    int _executedActionCount = 0;
};

AIChatController::AIChatController(Vehicle* vehicle, QObject* parent)
    : QObject(parent)
    , _vehicle(vehicle)
    , _networkManager(QGCNetworkHelper::createNetworkManager(this))
    , _messages(new QmlObjectListModel(this))
{
    qCDebug(AIChatControllerLog) << "Created for vehicle:" << (vehicle ? vehicle->id() : -1);

    // Connect voice input signal to handler
    connect(this, &AIChatController::voiceInputReceived, this, &AIChatController::_onVoiceInputReceived);

    // Update voice input availability when Groq API key changes
    auto* aiSettings = SettingsManager::instance()->aiSettings();
    connect(aiSettings->groqApiKey(), &Fact::rawValueChanged, this, &AIChatController::voiceInputAvailableChanged);

    // Initialize action queue timer for sequential command execution
    _actionQueueTimer = new QTimer(this);
    _actionQueueTimer->setInterval(250);  // Check every 250ms
    connect(_actionQueueTimer, &QTimer::timeout, this, &AIChatController::_processActionQueue);
}

AIChatController::~AIChatController()
{
    if (_pendingReply) {
        _pendingReply->abort();
    }
    if (_whisperReply) {
        _whisperReply->abort();
    }
    if (_mediaRecorder) {
        _mediaRecorder->stop();
        delete _mediaRecorder;
        _mediaRecorder = nullptr;
    }
    if (_captureSession) {
        delete _captureSession;
        _captureSession = nullptr;
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

    // Track that this was a typed message (not voice)
    _lastMessageWasVoice = false;

    // Add user message to chat
    _addMessage(MessageRole::User, userMessage);

    // Send to Claude API
    _sendToClaudeAPI(userMessage);
}

void AIChatController::executeCommand(int messageIndex, bool userInitiated)
{
    if (messageIndex < 0 || messageIndex >= _messages->count()) {
        qCWarning(AIChatControllerLog) << "Invalid message index:" << messageIndex;
        return;
    }

    auto* message = qobject_cast<AIChatMessage*>(_messages->get(messageIndex));
    if (!message || message->actions().isEmpty()) {
        qCWarning(AIChatControllerLog) << "No commands to execute at index:" << messageIndex;
        return;
    }

    // Clear any existing queue (new command takes priority)
    _clearActionQueue();

    // Queue all actions for sequential execution
    QVariantList actions = message->actions();
    _currentMessageIndex = messageIndex;

    // Immediately update UI to show execution is starting
    message->setCommandStatus(CommandStatus::Pending);
    message->setExecutedActionCount(0);

    qCDebug(AIChatControllerLog) << "==========================================================";
    qCDebug(AIChatControllerLog) << "EXECUTE COMMAND - Message index:" << messageIndex;
    qCDebug(AIChatControllerLog) << "  Message content:" << message->content();
    qCDebug(AIChatControllerLog) << "  Total actions:" << actions.count();
    qCDebug(AIChatControllerLog) << "  User initiated:" << userInitiated;
    qCDebug(AIChatControllerLog) << "==========================================================";

    for (int i = 0; i < actions.count(); ++i) {
        QVariantMap actionMap = actions[i].toMap();
        QueuedAction queuedAction;
        queuedAction.action = actionMap["action"].toString();
        queuedAction.parameters = actionMap["parameters"].toMap();
        _actionQueue.append(queuedAction);

        qCDebug(AIChatControllerLog) << "  [" << (i + 1) << "/" << actions.count() << "] Queued:"
                                      << queuedAction.action << "params:" << queuedAction.parameters;
    }

    // Start executing the queue
    _executeNextAction();
}

void AIChatController::_clearActionQueue()
{
    if (_isExecutingQueue) {
        qCDebug(AIChatControllerLog) << "Clearing action queue (had" << _actionQueue.count() << "pending actions)";
    }
    _actionQueue.clear();
    _actionQueueTimer->stop();
    _isExecutingQueue = false;
    _currentMessageIndex = -1;
}

void AIChatController::_executeNextAction()
{
    if (_actionQueue.isEmpty()) {
        qCDebug(AIChatControllerLog) << "==========================================================";
        qCDebug(AIChatControllerLog) << "ALL ACTIONS COMPLETE";
        qCDebug(AIChatControllerLog) << "==========================================================";
        _actionQueueTimer->stop();
        _isExecutingQueue = false;

        // Mark message as executed
        if (_currentMessageIndex >= 0 && _currentMessageIndex < _messages->count()) {
            auto* message = qobject_cast<AIChatMessage*>(_messages->get(_currentMessageIndex));
            if (message) {
                message->setCommandStatus(CommandStatus::Executed);
                qCDebug(AIChatControllerLog) << "  Executed" << message->executedActionCount() << "actions successfully";
            }
        }
        _currentMessageIndex = -1;
        return;
    }

    _isExecutingQueue = true;
    QueuedAction& currentAction = _actionQueue.first();

    // Get current message for context
    int totalActions = 0;
    int currentActionNum = 0;
    if (_currentMessageIndex >= 0 && _currentMessageIndex < _messages->count()) {
        auto* message = qobject_cast<AIChatMessage*>(_messages->get(_currentMessageIndex));
        if (message) {
            totalActions = message->actionCount();
            currentActionNum = message->executedActionCount() + 1;
        }
    }

    qCDebug(AIChatControllerLog) << "----------------------------------------------------------";
    qCDebug(AIChatControllerLog) << "EXECUTING ACTION [" << currentActionNum << "/" << totalActions << "]:" << currentAction.action;
    qCDebug(AIChatControllerLog) << "  Parameters:" << currentAction.parameters;
    qCDebug(AIChatControllerLog) << "  Remaining in queue:" << _actionQueue.count();

    // Store target position/altitude for completion checking
    if (currentAction.action == "fly_heading" || currentAction.action == "goto") {
        if (currentAction.action == "fly_heading") {
            double headingDeg = currentAction.parameters.value("heading_deg", 0).toDouble();
            double distanceM = currentAction.parameters.value("distance_m", 50).toDouble();
            QGeoCoordinate currentPos = _vehicle->coordinate();
            if (currentPos.isValid()) {
                currentAction.targetPosition = currentPos.atDistanceAndAzimuth(distanceM, headingDeg);
            }
        } else {
            double lat = currentAction.parameters.value("latitude").toDouble();
            double lon = currentAction.parameters.value("longitude").toDouble();
            currentAction.targetPosition = QGeoCoordinate(lat, lon);
        }
        if (currentAction.parameters.contains("altitude_m")) {
            currentAction.targetAltitude = currentAction.parameters.value("altitude_m").toDouble();
        } else if (_vehicle->altitudeRelative()) {
            currentAction.targetAltitude = _vehicle->altitudeRelative()->rawValue().toDouble();
        }
        qCDebug(AIChatControllerLog) << "  Target position:" << currentAction.targetPosition
                                      << "altitude:" << currentAction.targetAltitude << "m";
    } else if (currentAction.action == "change_altitude") {
        if (currentAction.parameters.contains("altitude_m")) {
            currentAction.targetAltitude = currentAction.parameters.value("altitude_m").toDouble();
        } else {
            double change = currentAction.parameters.value("change_m", 0).toDouble();
            double currentAlt = _vehicle->altitudeRelative() ? _vehicle->altitudeRelative()->rawValue().toDouble() : 0;
            currentAction.targetAltitude = currentAlt + change;
        }
        qCDebug(AIChatControllerLog) << "  Target altitude:" << currentAction.targetAltitude << "m";
    } else if (currentAction.action == "takeoff") {
        currentAction.targetAltitude = currentAction.parameters.value("altitude_m", 10).toDouble();
        qCDebug(AIChatControllerLog) << "  Target altitude:" << currentAction.targetAltitude << "m";
    }

    // Execute the command
    bool success = _executeVehicleCommand(currentAction.action, currentAction.parameters);

    if (!success) {
        qCWarning(AIChatControllerLog) << "  [RESULT] ACTION FAILED:" << currentAction.action;
        qCWarning(AIChatControllerLog) << "  Clearing remaining action queue";
        _clearActionQueue();

        // Mark message as failed
        if (_currentMessageIndex >= 0 && _currentMessageIndex < _messages->count()) {
            auto* message = qobject_cast<AIChatMessage*>(_messages->get(_currentMessageIndex));
            if (message) {
                message->setCommandStatus(CommandStatus::Failed);
            }
        }
        return;
    }

    qCDebug(AIChatControllerLog) << "  [RESULT] Command sent successfully";

    // Check if this action completes immediately or needs monitoring
    bool completesImmediately = (currentAction.action == "set_speed" ||
                                  currentAction.action == "pause" ||
                                  currentAction.action == "set_flight_mode" ||
                                  currentAction.action == "orbit" ||
                                  currentAction.action == "rtl" ||
                                  currentAction.action == "land" ||
                                  currentAction.action == "emergency_stop" ||
                                  currentAction.action == "start_mission" ||
                                  currentAction.action == "pause_mission" ||
                                  currentAction.action == "goto_waypoint" ||
                                  currentAction.action == "set_parameter" ||
                                  currentAction.action == "get_parameter" ||
                                  currentAction.action == "set_servo" ||
                                  currentAction.action == "change_heading" ||
                                  currentAction.action == "set_roi" ||
                                  currentAction.action == "stop_roi");

    if (completesImmediately) {
        qCDebug(AIChatControllerLog) << "  -> Action completes immediately (fire-and-forget)";
        _actionQueue.removeFirst();

        // Update executed count in UI
        if (_currentMessageIndex >= 0 && _currentMessageIndex < _messages->count()) {
            auto* message = qobject_cast<AIChatMessage*>(_messages->get(_currentMessageIndex));
            if (message) {
                message->setExecutedActionCount(message->executedActionCount() + 1);
                qCDebug(AIChatControllerLog) << "  -> Executed count:" << message->executedActionCount() << "/" << message->actionCount();
            }
        }

        // Add small delay before next action to allow MAVLink ACKs to be processed
        // This prevents "Waiting on previous response to same command" errors
        qCDebug(AIChatControllerLog) << "  -> Waiting 200ms before next action (MAVLink ACK delay)";
        QTimer::singleShot(200, this, &AIChatController::_executeNextAction);
    } else {
        // Start monitoring for completion
        qCDebug(AIChatControllerLog) << "  -> Monitoring for completion (polling every 250ms)...";
        if (_vehicle) {
            qCDebug(AIChatControllerLog) << "     Current vehicle state:";
            qCDebug(AIChatControllerLog) << "       Armed:" << _vehicle->armed();
            qCDebug(AIChatControllerLog) << "       Position:" << _vehicle->coordinate();
            if (_vehicle->altitudeRelative()) {
                qCDebug(AIChatControllerLog) << "       Altitude:" << _vehicle->altitudeRelative()->rawValue().toDouble() << "m";
            }
        }
        _actionQueueTimer->start();
    }
}

void AIChatController::_processActionQueue()
{
    if (_actionQueue.isEmpty() || !_isExecutingQueue) {
        _actionQueueTimer->stop();
        return;
    }

    if (_isCurrentActionComplete()) {
        const QueuedAction& completed = _actionQueue.first();
        qCDebug(AIChatControllerLog) << "  -> Action COMPLETE:" << completed.action;
        _actionQueue.removeFirst();
        _actionQueueTimer->stop();

        // Update executed count in UI
        if (_currentMessageIndex >= 0 && _currentMessageIndex < _messages->count()) {
            auto* message = qobject_cast<AIChatMessage*>(_messages->get(_currentMessageIndex));
            if (message) {
                message->setExecutedActionCount(message->executedActionCount() + 1);
                qCDebug(AIChatControllerLog) << "  -> Executed count:" << message->executedActionCount() << "/" << message->actionCount();
            }
        }

        _executeNextAction();
    }
}

bool AIChatController::_isCurrentActionComplete()
{
    if (_actionQueue.isEmpty() || !_vehicle) {
        return true;
    }

    const QueuedAction& current = _actionQueue.first();
    const double positionThreshold = 2.0;  // meters
    const double altitudeThreshold = 1.0;  // meters

    if (current.action == "arm") {
        return _vehicle->armed();
    }
    else if (current.action == "disarm") {
        return !_vehicle->armed();
    }
    else if (current.action == "takeoff") {
        double currentAlt = _vehicle->altitudeRelative() ? _vehicle->altitudeRelative()->rawValue().toDouble() : 0;
        bool complete = currentAlt >= (current.targetAltitude * 0.9);  // 90% of target
        if (complete) {
            qCDebug(AIChatControllerLog) << "  Takeoff complete: alt=" << currentAlt << "target=" << current.targetAltitude;
        }
        return complete;
    }
    else if (current.action == "change_altitude") {
        double currentAlt = _vehicle->altitudeRelative() ? _vehicle->altitudeRelative()->rawValue().toDouble() : 0;
        bool complete = qAbs(currentAlt - current.targetAltitude) < altitudeThreshold;
        if (complete) {
            qCDebug(AIChatControllerLog) << "  Altitude change complete: alt=" << currentAlt << "target=" << current.targetAltitude;
        }
        return complete;
    }
    else if (current.action == "fly_heading" || current.action == "goto") {
        if (!current.targetPosition.isValid()) {
            return true;  // No valid target, consider complete
        }
        QGeoCoordinate currentPos = _vehicle->coordinate();
        if (!currentPos.isValid()) {
            return false;
        }
        double distance = currentPos.distanceTo(current.targetPosition);
        bool complete = distance < positionThreshold;
        if (complete) {
            qCDebug(AIChatControllerLog) << "  Position reached: distance=" << distance << "m";
        }
        return complete;
    }

    // Default: consider complete
    return true;
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

Flight Control:
- arm: Arm the vehicle motors. Parameters: none
- disarm: Disarm the vehicle motors (only when not flying). Parameters: none
- takeoff: Take off to specified altitude. Parameters: altitude_m (number, required)
- land: Land at current position. Parameters: none
- rtl: Return to launch/home position. Parameters: smart_rtl (boolean, optional, default false)
- goto: Go to specified location. Parameters: latitude (number), longitude (number), altitude_m (number, optional)
- pause: Pause/hold current position. Parameters: none
- change_altitude: Change altitude to specific value OR relative change. Parameters: altitude_m (number, absolute altitude) OR change_m (number, relative change, can be negative)
- emergency_stop: EMERGENCY - Kill all motors immediately. Parameters: none
- set_flight_mode: Change flight mode. Parameters: mode_name (string)
- fly_heading: Fly in a compass direction. Parameters: heading_deg (number, 0=North, 90=East, 180=South, 270=West), distance_m (number), altitude_m (number, optional - use this instead of separate change_altitude)
- set_speed: Set flight speed. Parameters: speed_mps (number, meters per second)
- orbit: Circle around current position or a point (PX4 only). Parameters: radius_m (number), direction (string: "cw" or "ccw"), optional latitude/longitude to orbit around

Mission Control:
- start_mission: Start the loaded mission from the beginning. Parameters: none
- pause_mission: Pause the current mission (same as pause). Parameters: none
- goto_waypoint: Jump to a specific waypoint in the mission. Parameters: waypoint_index (number, 1-based as user sees them)

Parameters:
- set_parameter: Set a vehicle parameter. Parameters: name (string, e.g. "WP_RADIUS"), value (number or string)
- get_parameter: Get a vehicle parameter value. Parameters: name (string, e.g. "WP_RADIUS")

Hardware:
- set_servo: Set a servo to a specific PWM value. Parameters: channel (number, 1-16), pwm (number, typically 1000-2000)

Camera/Gimbal:
- change_heading: Rotate the vehicle to face a specific compass direction. Parameters: heading_deg (number, 0=North, 90=East, 180=South, 270=West)
- set_roi: Set Region of Interest - vehicle/gimbal will point at this location. Parameters: latitude (number), longitude (number), altitude_m (number, optional)
- stop_roi: Cancel ROI tracking, return camera to normal. Parameters: none

RESPONSE FORMAT (always respond with valid JSON):
{
    "understood": true,
    "actions": [
        {"action": "command_name", "parameters": {...}},
        {"action": "another_command", "parameters": {...}}
    ],
    "message": "Human-readable response to user",
    "confirmation_needed": false
}

For single actions, you can still use the simpler format:
{
    "understood": true,
    "action": "command_name",
    "parameters": {},
    "message": "...",
    "confirmation_needed": false
}

MODE REQUIREMENTS:
- Mission commands (start_mission, goto_waypoint) require AUTO mode. If not in AUTO, add set_flight_mode with mode_name="Auto" BEFORE the mission command.
- Manual flight commands (goto, fly_heading, change_altitude, pause, orbit) require GUIDED mode. If not in GUIDED, add set_flight_mode with mode_name="Guided" BEFORE the command.
- takeoff requires GUIDED mode. Add set_flight_mode if needed.
- rtl and land work from any mode.
- set_parameter, get_parameter, set_servo work from any mode.
- Always check the current Flight Mode in the vehicle state and prepend mode changes as needed.

CAPABILITY CHECK:
- Check "Supported Capabilities" in vehicle state before using: orbit, change_heading, ROI commands.
- If a capability is not listed, do NOT use that command - explain to user it's not supported by their firmware/vehicle.

RULES:
- If you cannot understand the request or it's just a question, set action/actions to null/empty
- Always set confirmation_needed=true for: arm, takeoff, emergency_stop, disarm
- Never execute disarm while the vehicle is flying
- Validate altitude requests are reasonable (typically 2-400m)
- If unsure about the user's intent, ask for clarification in the message field
- Be concise - keep messages to 1 sentence
- Never mention latitude/longitude coordinates in your message responses
- Multiple actions are executed sequentially - each waits for the previous to complete
- For complex maneuvers, you can chain multiple actions (e.g., change_altitude then fly_heading))";

    if (includeState && _vehicle) {
        prompt += "\n\nCURRENT VEHICLE STATE:\n" + _getVehicleStateContext();
    }

    // Add custom context if provided
    QString customContext = aiSettings->customContext()->rawValue().toString().trimmed();
    if (!customContext.isEmpty()) {
        prompt += "\n\nADDITIONAL CONTEXT:\n" + customContext;
    }

    return prompt;
}

QString AIChatController::_getVehicleStateContext() const
{
    if (!_vehicle) {
        return "No vehicle connected";
    }

    QString state;

    // Firmware and vehicle type
    state += QString("- Firmware: %1\n").arg(_vehicle->firmwareTypeString());
    state += QString("- Vehicle Type: %1\n").arg(_vehicle->vehicleTypeString());

    // Basic state
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

    state += QString("- ROI Active: %1\n").arg(_vehicle->isROIEnabled() ? "Yes" : "No");

    // Supported capabilities (so AI knows what commands will work)
    QStringList capabilities;
    if (_vehicle->orbitModeSupported()) capabilities << "orbit";
    if (_vehicle->roiModeSupported()) capabilities << "ROI";
    if (_vehicle->changeHeadingSupported()) capabilities << "change_heading";
    if (_vehicle->pauseVehicleSupported()) capabilities << "pause";
    if (_vehicle->guidedModeSupported()) capabilities << "guided_mode";

    state += QString("- Supported Capabilities: %1\n").arg(capabilities.isEmpty() ? "none" : capabilities.join(", "));

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
    bool confirmationNeeded = aiResponse["confirmation_needed"].toBool();

    // Parse actions - support both single "action" and multiple "actions" array
    QVariantList actionsList;

    if (aiResponse.contains("actions") && aiResponse["actions"].isArray()) {
        // Multiple actions format
        QJsonArray actionsArray = aiResponse["actions"].toArray();
        for (const QJsonValue& actionVal : actionsArray) {
            QJsonObject actionObj = actionVal.toObject();
            QVariantMap actionEntry;
            actionEntry["action"] = actionObj["action"].toString();
            actionEntry["parameters"] = actionObj["parameters"].toObject().toVariantMap();
            if (!actionEntry["action"].toString().isEmpty()) {
                actionsList.append(actionEntry);
            }
        }
        qCDebug(AIChatControllerLog) << "Parsed" << actionsList.count() << "actions";
    } else if (aiResponse.contains("action") && !aiResponse["action"].toString().isEmpty()) {
        // Single action format (backwards compatible)
        QVariantMap actionEntry;
        actionEntry["action"] = aiResponse["action"].toString();
        actionEntry["parameters"] = aiResponse["parameters"].toObject().toVariantMap();
        actionsList.append(actionEntry);
        qCDebug(AIChatControllerLog) << "Parsed single action:" << actionEntry["action"].toString();
    }

    // If message is empty but we have valid JSON, create a default message
    if (message.isEmpty()) {
        if (!actionsList.isEmpty()) {
            if (actionsList.count() == 1) {
                message = tr("Command: %1").arg(actionsList.first().toMap()["action"].toString());
            } else {
                message = tr("%1 commands queued").arg(actionsList.count());
            }
        } else {
            message = tr("(No response message)");
        }
    }

    // Check if any action is dangerous and requires confirmation
    auto* aiSettings = SettingsManager::instance()->aiSettings();
    bool forceConfirm = aiSettings->confirmDangerousCommands()->rawValue().toBool();

    if (forceConfirm) {
        for (const QVariant& actionVar : actionsList) {
            QString actionName = actionVar.toMap()["action"].toString();
            if (_isDangerousCommand(actionName)) {
                confirmationNeeded = true;
                break;
            }
        }
    }

    // Add the AI's message with all actions
    _addMessage(MessageRole::Assistant, message, actionsList, confirmationNeeded);

    // Auto-execute if no confirmation needed and we have actions
    if (!actionsList.isEmpty() && !confirmationNeeded) {
        int lastIndex = _messages->count() - 1;
        executeCommand(lastIndex, false);  // false = not user initiated, no voice feedback
    }
}

void AIChatController::_addMessage(MessageRole role, const QString& content, const QString& action,
                                    const QVariantMap& parameters, bool requiresConfirmation)
{
    auto* message = new AIChatMessage(role, content, action, parameters, requiresConfirmation, this);
    _messages->append(message);

    // Speak assistant messages using TTS if not muted
    if (role == MessageRole::Assistant) {
        _speakMessage(content);
    }
}

void AIChatController::_addMessage(MessageRole role, const QString& content, const QVariantList& actions,
                                    bool requiresConfirmation)
{
    auto* message = new AIChatMessage(role, content, actions, requiresConfirmation, this);
    _messages->append(message);

    // Speak assistant messages using TTS if not muted
    if (role == MessageRole::Assistant) {
        _speakMessage(content);
    }
}

void AIChatController::_speakMessage(const QString& text)
{
    // Only speak responses to voice messages
    if (!_lastMessageWasVoice) {
        return;
    }

    auto* aiSettings = SettingsManager::instance()->aiSettings();

    // Check if TTS is enabled in AI settings
    if (!aiSettings->enableTextToSpeech()->rawValue().toBool()) {
        return;
    }

    // Speak the message (ignores global mute - Titan AI TTS is independent)
    // Speech rate: -1.0 slowest to 1.0 fastest, 0.0 is normal
    AudioOutput::instance()->say(text, AudioOutput::TextMod::None, true, 0.05);
}

bool AIChatController::_executeVehicleCommand(const QString& action, const QVariantMap& parameters)
{
    if (!_vehicle) {
        qCWarning(AIChatControllerLog) << "  [VEHICLE CMD] FAILED - No vehicle connected";
        return false;
    }

    qCDebug(AIChatControllerLog) << "  [VEHICLE CMD] Sending:" << action;
    qCDebug(AIChatControllerLog) << "    Parameters:" << parameters;

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
        // Support both absolute altitude_m and relative change_m
        if (parameters.contains("altitude_m")) {
            double targetAlt = parameters.value("altitude_m").toDouble();
            double currentAlt = _vehicle->altitudeRelative() ? _vehicle->altitudeRelative()->rawValue().toDouble() : 0;
            double change = targetAlt - currentAlt;
            qCDebug(AIChatControllerLog) << "  change_altitude: target=" << targetAlt << "m, current=" << currentAlt << "m, change=" << change << "m";
            _vehicle->guidedModeChangeAltitude(change, true);
        } else {
            double change = parameters.value("change_m", 0).toDouble();
            qCDebug(AIChatControllerLog) << "  change_altitude: relative change=" << change << "m";
            _vehicle->guidedModeChangeAltitude(change, true);
        }
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
    else if (action == "fly_heading") {
        // Fly in a compass direction for a given distance
        double headingDeg = parameters.value("heading_deg", 0).toDouble();
        double distanceM = parameters.value("distance_m", 50).toDouble();

        QGeoCoordinate currentPos = _vehicle->coordinate();
        if (!currentPos.isValid()) {
            qCWarning(AIChatControllerLog) << "Cannot fly heading - no valid position";
            return false;
        }

        // Calculate destination coordinate using heading and distance
        QGeoCoordinate destination = currentPos.atDistanceAndAzimuth(distanceM, headingDeg);

        // Check if there's a target altitude specified, otherwise preserve current
        double targetAlt = _vehicle->altitudeRelative() ? _vehicle->altitudeRelative()->rawValue().toDouble() : 0;
        if (parameters.contains("altitude_m")) {
            targetAlt = parameters.value("altitude_m").toDouble();
        }
        destination.setAltitude(targetAlt);

        qCDebug(AIChatControllerLog) << "  fly_heading: heading=" << headingDeg << "deg, distance=" << distanceM << "m";
        qCDebug(AIChatControllerLog) << "  fly_heading: from=" << currentPos << "to=" << destination << "alt=" << targetAlt << "m";
        _vehicle->guidedModeGotoLocation(destination);
        return true;
    }
    else if (action == "set_speed") {
        double speedMps = parameters.value("speed_mps", 5).toDouble();
        _vehicle->guidedModeChangeGroundSpeedMetersSecond(speedMps);
        return true;
    }
    else if (action == "orbit") {
        if (!_vehicle->orbitModeSupported()) {
            qCWarning(AIChatControllerLog) << "orbit: not supported by this vehicle/firmware";
            return false;
        }

        double radius = parameters.value("radius_m", 20).toDouble();
        QString direction = parameters.value("direction", "cw").toString();

        // Use current position if no center specified
        QGeoCoordinate center;
        if (parameters.contains("latitude") && parameters.contains("longitude")) {
            center = QGeoCoordinate(parameters.value("latitude").toDouble(),
                                     parameters.value("longitude").toDouble());
        } else {
            center = _vehicle->coordinate();
        }

        if (!center.isValid()) {
            qCWarning(AIChatControllerLog) << "orbit: no valid center position";
            return false;
        }

        // Get current altitude
        double altitude = _vehicle->altitudeAMSL() ? _vehicle->altitudeAMSL()->rawValue().toDouble() : 0;

        // Negative radius for counter-clockwise
        double signedRadius = (direction == "ccw") ? -radius : radius;

        _vehicle->guidedModeOrbit(center, signedRadius, altitude);
        return true;
    }
    else if (action == "start_mission") {
        _vehicle->startMission();
        return true;
    }
    else if (action == "pause_mission") {
        _vehicle->pauseVehicle();
        return true;
    }
    else if (action == "goto_waypoint") {
        // User sees waypoints as 1, 2, 3... which matches the mission sequence directly
        int waypointIndex = parameters.value("waypoint_index", 0).toInt();
        _vehicle->setCurrentMissionSequence(waypointIndex);
        return true;
    }
    else if (action == "set_parameter") {
        QString paramName = parameters.value("name").toString();
        QVariant paramValue = parameters.value("value");

        if (paramName.isEmpty()) {
            qCWarning(AIChatControllerLog) << "set_parameter: missing parameter name";
            return false;
        }

        ParameterManager* paramMgr = _vehicle->parameterManager();
        if (!paramMgr) {
            qCWarning(AIChatControllerLog) << "set_parameter: no parameter manager";
            return false;
        }

        // Use default component ID
        int compId = ParameterManager::defaultComponentId;
        if (!paramMgr->parameterExists(compId, paramName)) {
            qCWarning(AIChatControllerLog) << "set_parameter: parameter does not exist:" << paramName;
            return false;
        }

        Fact* param = paramMgr->getParameter(compId, paramName);
        if (!param) {
            qCWarning(AIChatControllerLog) << "set_parameter: failed to get parameter:" << paramName;
            return false;
        }

        qCDebug(AIChatControllerLog) << "  set_parameter:" << paramName << "=" << paramValue;
        param->setRawValue(paramValue);
        return true;
    }
    else if (action == "get_parameter") {
        QString paramName = parameters.value("name").toString();

        if (paramName.isEmpty()) {
            qCWarning(AIChatControllerLog) << "get_parameter: missing parameter name";
            return false;
        }

        ParameterManager* paramMgr = _vehicle->parameterManager();
        if (!paramMgr) {
            qCWarning(AIChatControllerLog) << "get_parameter: no parameter manager";
            return false;
        }

        int compId = ParameterManager::defaultComponentId;
        if (!paramMgr->parameterExists(compId, paramName)) {
            qCWarning(AIChatControllerLog) << "get_parameter: parameter does not exist:" << paramName;
            return false;
        }

        Fact* param = paramMgr->getParameter(compId, paramName);
        if (!param) {
            qCWarning(AIChatControllerLog) << "get_parameter: failed to get parameter:" << paramName;
            return false;
        }

        qCDebug(AIChatControllerLog) << "  get_parameter:" << paramName << "=" << param->rawValue();
        // For get_parameter, the value is logged but doesn't need any action
        // The AI will need to respond to the user with the value
        return true;
    }
    else if (action == "set_servo") {
        int channel = parameters.value("channel", 1).toInt();
        int pwm = parameters.value("pwm", 1500).toInt();

        if (channel < 1 || channel > 16) {
            qCWarning(AIChatControllerLog) << "set_servo: invalid channel:" << channel;
            return false;
        }

        qCDebug(AIChatControllerLog) << "  set_servo: channel=" << channel << "pwm=" << pwm;

        // MAV_CMD_DO_SET_SERVO = 183
        // param1 = servo channel, param2 = PWM value
        // showError=false to avoid dialog popups for AI-initiated commands
        _vehicle->sendCommand(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_DO_SET_SERVO, false,
                               static_cast<double>(channel), static_cast<double>(pwm));
        return true;
    }
    else if (action == "change_heading") {
        double headingDeg = parameters.value("heading_deg", 0).toDouble();

        QGeoCoordinate currentPos = _vehicle->coordinate();
        if (!currentPos.isValid()) {
            qCWarning(AIChatControllerLog) << "change_heading: no valid position";
            return false;
        }

        // Calculate a point 1000m away in the desired heading direction
        QGeoCoordinate headingCoord = currentPos.atDistanceAndAzimuth(1000, headingDeg);

        qCDebug(AIChatControllerLog) << "  change_heading:" << headingDeg << "deg -> coord:" << headingCoord;
        _vehicle->guidedModeChangeHeading(headingCoord);
        return true;
    }
    else if (action == "set_roi") {
        double lat = parameters.value("latitude").toDouble();
        double lon = parameters.value("longitude").toDouble();
        double alt = parameters.value("altitude_m", 0).toDouble();

        QGeoCoordinate roiCoord(lat, lon, alt);
        if (!roiCoord.isValid()) {
            qCWarning(AIChatControllerLog) << "set_roi: invalid coordinates:" << lat << lon;
            return false;
        }

        qCDebug(AIChatControllerLog) << "  set_roi:" << roiCoord;
        _vehicle->guidedModeROI(roiCoord);
        return true;
    }
    else if (action == "stop_roi") {
        qCDebug(AIChatControllerLog) << "  stop_roi";
        _vehicle->stopGuidedModeROI();
        return true;
    }

    qCWarning(AIChatControllerLog) << "  [VEHICLE CMD] FAILED - Unknown action:" << action;
    return false;
}

bool AIChatController::_isDangerousCommand(const QString& action)
{
    static const QStringList dangerousCommands = {
        "arm", "disarm", "takeoff", "emergency_stop"
    };
    return dangerousCommands.contains(action);
}

bool AIChatController::voiceInputAvailable() const
{
    // Voice input is available if we have an audio input device and Groq API key
    auto* aiSettings = SettingsManager::instance()->aiSettings();
    QString apiKey = aiSettings->groqApiKey()->rawValue().toString();

    return !QMediaDevices::audioInputs().isEmpty() && !apiKey.isEmpty();
}

void AIChatController::startListening()
{
    if (_isListening || _isProcessing) {
        return;
    }

    auto* aiSettings = SettingsManager::instance()->aiSettings();
    QString apiKey = aiSettings->groqApiKey()->rawValue().toString();
    if (apiKey.isEmpty()) {
        qCWarning(AIChatControllerLog) << "Groq API key not configured for voice input";
        return;
    }

    qCDebug(AIChatControllerLog) << "Starting voice input";
    _isListening = true;
    emit isListeningChanged();

    _startAudioRecording();
}

void AIChatController::stopListening()
{
    if (!_isListening) {
        return;
    }

    qCDebug(AIChatControllerLog) << "Stopping voice input";

    // Stop recording - the actual send happens when recorder state changes to StoppedState
    if (_mediaRecorder) {
        _mediaRecorder->stop();
    } else {
        _isListening = false;
        emit isListeningChanged();
    }
}

void AIChatController::_onVoiceInputReceived(const QString& text)
{
    qCDebug(AIChatControllerLog) << "Voice input received:" << text;

    _isListening = false;
    emit isListeningChanged();

    _recognizedText = text;
    emit recognizedTextChanged();

    if (!text.isEmpty()) {
        // Send the recognized text as a message
        sendMessage(text);
        // Mark as voice message after sendMessage (which resets to false)
        // This will be checked when the async API response arrives
        _lastMessageWasVoice = true;
    }
}

void AIChatController::_startAudioRecording()
{
    // Create a temporary file for the audio recording
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    _audioFilePath = QDir(tempDir).filePath("titan_voice_input.wav");

    qCDebug(AIChatControllerLog) << "Recording audio to:" << _audioFilePath;

    // Set up capture session
    _captureSession = new QMediaCaptureSession(this);

    // Set up audio input
    QAudioInput* audioInput = new QAudioInput(this);
    audioInput->setDevice(QMediaDevices::defaultAudioInput());
    _captureSession->setAudioInput(audioInput);

    // Set up media recorder
    _mediaRecorder = new QMediaRecorder(this);
    _captureSession->setRecorder(_mediaRecorder);

    // Configure recording format - WAV is well supported by Whisper
    _mediaRecorder->setOutputLocation(QUrl::fromLocalFile(_audioFilePath));
    _mediaRecorder->setMediaFormat(QMediaFormat::Wave);
    _mediaRecorder->setAudioSampleRate(16000);  // Whisper works well with 16kHz
    _mediaRecorder->setAudioChannelCount(1);    // Mono

    // Connect to recorder state changes - send audio when recording stops
    connect(_mediaRecorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        qCDebug(AIChatControllerLog) << "Recorder state changed:" << state;
        if (state == QMediaRecorder::StoppedState && _isListening) {
            // Recording finished, now send to Whisper
            _stopAudioRecording();
            _sendAudioToWhisper();
        }
    });

    connect(_mediaRecorder, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error error, const QString& errorString) {
        qCWarning(AIChatControllerLog) << "Recording error:" << error << errorString;
        _stopAudioRecording();
        _isListening = false;
        emit isListeningChanged();
    });

    // Start recording
    _mediaRecorder->record();
    qCDebug(AIChatControllerLog) << "Audio recording started - speak now";
}

void AIChatController::_stopAudioRecording()
{
    if (_mediaRecorder) {
        _mediaRecorder->deleteLater();
        _mediaRecorder = nullptr;
    }

    if (_captureSession) {
        _captureSession->deleteLater();
        _captureSession = nullptr;
    }
}

void AIChatController::_sendAudioToWhisper()
{
    auto* aiSettings = SettingsManager::instance()->aiSettings();
    QString apiKey = aiSettings->groqApiKey()->rawValue().toString();

    if (apiKey.isEmpty()) {
        qCWarning(AIChatControllerLog) << "Groq API key not configured";
        _isListening = false;
        emit isListeningChanged();
        return;
    }

    QFile* audioFile = new QFile(_audioFilePath);
    if (!audioFile->open(QIODevice::ReadOnly)) {
        qCWarning(AIChatControllerLog) << "Failed to open audio file:" << _audioFilePath;
        delete audioFile;
        _isListening = false;
        emit isListeningChanged();
        return;
    }

    qCDebug(AIChatControllerLog) << "Sending audio to Groq Whisper API, file size:" << audioFile->size();

    // Create multipart request
    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // Add the audio file
    QHttpPart audioPart;
    audioPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("audio/wav"));
    audioPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant("form-data; name=\"file\"; filename=\"audio.wav\""));
    audioPart.setBodyDevice(audioFile);
    audioFile->setParent(multiPart);  // Ensure file is deleted with multipart
    multiPart->append(audioPart);

    // Add the model parameter - Groq uses whisper-large-v3-turbo
    QHttpPart modelPart;
    modelPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"model\""));
    modelPart.setBody("whisper-large-v3-turbo");
    multiPart->append(modelPart);

    // Create request - Groq's OpenAI-compatible endpoint
    QUrl url("https://api.groq.com/openai/v1/audio/transcriptions");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

    // Send request
    _whisperReply = _networkManager->post(request, multiPart);
    multiPart->setParent(_whisperReply);  // Ensure multipart is deleted with reply

    connect(_whisperReply, &QNetworkReply::finished, this, &AIChatController::_onWhisperReplyFinished);
}

void AIChatController::_onWhisperReplyFinished()
{
    _isListening = false;
    emit isListeningChanged();

    if (!_whisperReply) {
        return;
    }

    QNetworkReply* reply = _whisperReply;
    _whisperReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        qCWarning(AIChatControllerLog) << "Groq Whisper API request failed:" << error;

        QByteArray responseData = reply->readAll();
        qCWarning(AIChatControllerLog) << "Groq API response:" << responseData;
    } else {
        QByteArray responseData = reply->readAll();
        qCDebug(AIChatControllerLog) << "Groq Whisper API response:" << responseData;

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            QString text = doc.object()["text"].toString().trimmed();
            if (!text.isEmpty()) {
                qCDebug(AIChatControllerLog) << "Transcribed text:" << text;
                emit voiceInputReceived(text);
            }
        }
    }

    reply->deleteLater();

    // Clean up the temp audio file
    QFile::remove(_audioFilePath);
}

#include "AIChatController.moc"

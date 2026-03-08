#include "AIChatController.h"

#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "AISettings.h"
#include "AppSettings.h"
#include "AISystemPrompt.h"
#include "AudioOutput.h"
#include "POIItem.h"
#include "Fact.h"
#include "Vehicle.h"
#include "ParameterManager.h"
#include "QGCNetworkHelper.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
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
    , _pois(new QmlObjectListModel(this))
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

    _asyncWaitTimer = new QTimer(this);
    _asyncWaitTimer->setSingleShot(true);
    connect(_asyncWaitTimer, &QTimer::timeout, this, &AIChatController::_onAsyncWaitComplete);

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
    _asyncWaitTimer->stop();
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

    // Handle wait/delay action - no vehicle command, just a timed delay
    if (currentAction.action == "wait") {
        int durationMs = qRound(currentAction.parameters.value("duration_s", 5).toDouble() * 1000);
        qCDebug(AIChatControllerLog) << "  -> Waiting for" << durationMs << "ms before next action";
        _actionQueue.removeFirst();

        // Update executed count in UI
        if (_currentMessageIndex >= 0 && _currentMessageIndex < _messages->count()) {
            auto* message = qobject_cast<AIChatMessage*>(_messages->get(_currentMessageIndex));
            if (message) {
                message->setExecutedActionCount(message->executedActionCount() + 1);
            }
        }

        QTimer::singleShot(durationMs, this, &AIChatController::_executeNextAction);
        return;
    }

    // Handle async_wait - starts a timer then immediately executes the next action.
    // When the timer fires, it skips the currently running action and proceeds to the one after it.
    if (currentAction.action == "async_wait") {
        int durationMs = qRound(currentAction.parameters.value("duration_s", 5).toDouble() * 1000);
        qCDebug(AIChatControllerLog) << "  -> Async wait for" << durationMs << "ms, proceeding to next action immediately";
        _actionQueue.removeFirst();

        // Update executed count in UI
        if (_currentMessageIndex >= 0 && _currentMessageIndex < _messages->count()) {
            auto* message = qobject_cast<AIChatMessage*>(_messages->get(_currentMessageIndex));
            if (message) {
                message->setExecutedActionCount(message->executedActionCount() + 1);
            }
        }

        // Start the async timer, then immediately execute the next action
        _asyncWaitTimer->start(durationMs);
        _executeNextAction();
        return;
    }

    // Execute the command
    bool success = _executeVehicleCommand(currentAction.action, currentAction.parameters);

    if (!success) {
        qCWarning(AIChatControllerLog) << "  [RESULT] ACTION FAILED:" << currentAction.action;
        qCWarning(AIChatControllerLog) << "  Clearing remaining action queue";

        // Save message index before clearing queue (which resets _currentMessageIndex to -1)
        int failedMessageIndex = _currentMessageIndex;
        _clearActionQueue();

        // Mark message as failed
        if (failedMessageIndex >= 0 && failedMessageIndex < _messages->count()) {
            auto* message = qobject_cast<AIChatMessage*>(_messages->get(failedMessageIndex));
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
                                  currentAction.action == "change_heading");

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

void AIChatController::_onAsyncWaitComplete()
{
    if (_actionQueue.isEmpty() || !_isExecutingQueue) {
        return;
    }

    qCDebug(AIChatControllerLog) << "  -> Async wait complete, skipping current action:" << _actionQueue.first().action;

    // Stop monitoring the current action
    _actionQueueTimer->stop();

    // Remove the currently executing action (skip it)
    _actionQueue.removeFirst();

    // Update executed count in UI for the skipped action
    if (_currentMessageIndex >= 0 && _currentMessageIndex < _messages->count()) {
        auto* message = qobject_cast<AIChatMessage*>(_messages->get(_currentMessageIndex));
        if (message) {
            message->setExecutedActionCount(message->executedActionCount() + 1);
        }
    }

    // Proceed to the next action
    _executeNextAction();
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
        if (!_vehicle->armed()) {
            return false;
        }
        // For planes in Takeoff mode, wait until reaching 90% of TKOFF_ALT
        if (_vehicle->fixedWing() &&
            _vehicle->flightMode().contains("Takeoff", Qt::CaseInsensitive)) {
            double currentAlt = _vehicle->altitudeRelative() ? _vehicle->altitudeRelative()->rawValue().toDouble() : 0;
            double tkoffAlt = 0;

            // Read TKOFF_ALT parameter for target altitude
            ParameterManager* paramMgr = _vehicle->parameterManager();
            int compId = ParameterManager::defaultComponentId;
            if (paramMgr->parameterExists(compId, "TKOFF_ALT")) {
                tkoffAlt = paramMgr->getParameter(compId, "TKOFF_ALT")->rawValue().toDouble();
            }

            // Fall back to 30m if parameter not available
            if (tkoffAlt <= 0) {
                tkoffAlt = 30;
            }

            bool complete = currentAlt >= (tkoffAlt * 0.9);
            if (complete) {
                qCDebug(AIChatControllerLog) << "  Plane takeoff complete: alt=" << currentAlt << "target=" << tkoffAlt;
            }
            return complete;
        }
        return true;
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

    // Build request body with prompt caching for the static system prompt
    QJsonObject requestBody;
    requestBody["model"] = "claude-sonnet-4-5-20250929";
    requestBody["max_tokens"] = 1024;

    // Use array format for system prompt to enable prompt caching
    // The static prompt is cached; dynamic vehicle state is appended fresh each request
    QJsonArray systemArray;
    QJsonObject staticPrompt;
    staticPrompt["type"] = "text";
    staticPrompt["text"] = QString::fromLatin1(kAISystemPrompt);
    staticPrompt["cache_control"] = QJsonObject{{"type", "ephemeral"}};
    systemArray.append(staticPrompt);

    // Append dynamic context (vehicle state + custom context)
    QString dynamicContext = _buildDynamicContext();
    if (!dynamicContext.isEmpty()) {
        QJsonObject dynamicPrompt;
        dynamicPrompt["type"] = "text";
        dynamicPrompt["text"] = dynamicContext;
        systemArray.append(dynamicPrompt);
    }

    requestBody["system"] = systemArray;
    requestBody["messages"] = messagesArray;

    QByteArray requestData = QJsonDocument(requestBody).toJson();
    qCDebug(AIChatControllerLog) << "Sending request to Claude API";

    _pendingReply = _networkManager->post(request, requestData);
    connect(_pendingReply, &QNetworkReply::finished, this, &AIChatController::_onNetworkReplyFinished);
}

QString AIChatController::_buildDynamicContext() const
{
    auto* aiSettings = SettingsManager::instance()->aiSettings();
    bool includeState = aiSettings->includeVehicleState()->rawValue().toBool();

    QString context;

    if (includeState && _vehicle) {
        context += "CURRENT VEHICLE STATE:\n" + _getVehicleStateContext();
    }

    // Add POIs if any exist
    if (_pois->count() > 0) {
        if (!context.isEmpty()) context += "\n";
        context += "POINTS OF INTEREST (POIs):\n";
        for (int i = 0; i < _pois->count(); ++i) {
            auto* poi = qobject_cast<POIItem*>(_pois->get(i));
            if (poi) {
                context += QString("- POI %1: %2, %3\n")
                    .arg(poi->number())
                    .arg(poi->coordinate().latitude(), 0, 'f', 10)
                    .arg(poi->coordinate().longitude(), 0, 'f', 10);
            }
        }
    }

    // Add custom context if provided
    QString customContext = aiSettings->customContext()->rawValue().toString().trimmed();
    if (!customContext.isEmpty()) {
        if (!context.isEmpty()) context += "\n";
        context += "ADDITIONAL CONTEXT:\n" + customContext;
    }

    return context;
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
    if (_vehicle->heading()) {
        state += QString("- Heading: %1 deg\n").arg(_vehicle->heading()->rawValue().toDouble(), 0, 'f', 1);
    }

    QGeoCoordinate coord = _vehicle->coordinate();
    if (coord.isValid()) {
        state += QString("- Position: %1, %2\n").arg(coord.latitude(), 0, 'f', 10).arg(coord.longitude(), 0, 'f', 10);
    }

    QGeoCoordinate home = _vehicle->homePosition();
    if (home.isValid()) {
        state += QString("- Home Position: %1, %2\n").arg(home.latitude(), 0, 'f', 10).arg(home.longitude(), 0, 'f', 10);
        if (coord.isValid()) {
            state += QString("- Distance to Home: %1 m\n").arg(coord.distanceTo(home), 0, 'f', 1);
            state += QString("- Bearing to Home: %1 deg\n").arg(coord.azimuthTo(home), 0, 'f', 1);
        }
    }


    // Supported capabilities (so AI knows what commands will work)
    QStringList capabilities;
    if (_vehicle->orbitModeSupported()) capabilities << "orbit";

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
    // Remove closing ``` and anything after it (AI sometimes adds notes after the code block)
    int closingBackticks = cleanedContent.indexOf("```");
    if (closingBackticks >= 0) {
        cleanedContent = cleanedContent.left(closingBackticks);
    }
    cleanedContent = cleanedContent.trimmed();

    // If the AI added extra text after the JSON object, extract just the JSON
    // Find the matching closing brace for the outermost JSON object
    if (cleanedContent.startsWith("{")) {
        int braceDepth = 0;
        int jsonEnd = -1;
        for (int i = 0; i < cleanedContent.length(); ++i) {
            if (cleanedContent[i] == '{') braceDepth++;
            else if (cleanedContent[i] == '}') {
                braceDepth--;
                if (braceDepth == 0) {
                    jsonEnd = i;
                    break;
                }
            }
        }
        if (jsonEnd >= 0 && jsonEnd < cleanedContent.length() - 1) {
            qCDebug(AIChatControllerLog) << "Trimming extra text after JSON object";
            cleanedContent = cleanedContent.left(jsonEnd + 1);
        }
    }

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

    // Log parsed actions in readable format
    if (!actionsList.isEmpty()) {
        qCDebug(AIChatControllerLog) << "AI response:" << message;
        qCDebug(AIChatControllerLog) << "Actions (" << actionsList.count() << "):";
        for (int i = 0; i < actionsList.count(); ++i) {
            QVariantMap entry = actionsList[i].toMap();
            QString actionName = entry["action"].toString();
            QVariantMap params = entry["parameters"].toMap();
            QStringList paramStrs;
            for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
                paramStrs << QString("%1=%2").arg(it.key(), it.value().toString());
            }
            qCDebug(AIChatControllerLog) << "  " << (i + 1) << "." << actionName << (paramStrs.isEmpty() ? "" : "(" + paramStrs.join(", ") + ")");
        }
    } else {
        qCDebug(AIChatControllerLog) << "AI response (no actions):" << message;
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
        double currentAlt = _vehicle->altitudeRelative() ? _vehicle->altitudeRelative()->rawValue().toDouble() : 0;
        double targetAlt;

        if (parameters.contains("altitude_m")) {
            targetAlt = parameters.value("altitude_m").toDouble();
        } else {
            double change = parameters.value("change_m", 0).toDouble();
            targetAlt = currentAlt + change;
        }

        double change = targetAlt - currentAlt;
        bool immediate = parameters.value("immediate", true).toBool();
        qCDebug(AIChatControllerLog) << "  change_altitude: target=" << targetAlt << "m, current=" << currentAlt << "m, change=" << change << "m, immediate=" << immediate;

        _vehicle->guidedModeChangeAltitude(change, immediate);
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
        // Try exact name first, then uppercase (firmware params are typically uppercase)
        if (!paramMgr->parameterExists(compId, paramName)) {
            QString upperName = paramName.toUpper();
            if (paramMgr->parameterExists(compId, upperName)) {
                paramName = upperName;
            } else {
                qCWarning(AIChatControllerLog) << "set_parameter: parameter does not exist:" << paramName;
                return false;
            }
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
            QString upperName = paramName.toUpper();
            if (paramMgr->parameterExists(compId, upperName)) {
                paramName = upperName;
            } else {
                qCWarning(AIChatControllerLog) << "get_parameter: parameter does not exist:" << paramName;
                return false;
            }
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

    // Stop any active TTS so it doesn't interfere with voice input
    AudioOutput::instance()->stop();

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

void AIChatController::addPOI(double latitude, double longitude)
{
    int number = _pois->count() + 1;
    auto* poi = new POIItem(QGeoCoordinate(latitude, longitude), number, this);
    _pois->append(poi);
    qCDebug(AIChatControllerLog) << "Added POI" << number << "at" << latitude << longitude;
}

void AIChatController::removePOI(int index)
{
    if (index < 0 || index >= _pois->count()) {
        return;
    }

    qCDebug(AIChatControllerLog) << "Removing POI" << (index + 1);
    _pois->removeAt(index);

    // Renumber remaining POIs
    for (int i = 0; i < _pois->count(); ++i) {
        auto* poi = qobject_cast<POIItem*>(_pois->get(i));
        if (poi) {
            poi->setNumber(i + 1);
        }
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

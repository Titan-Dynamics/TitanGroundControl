#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtNetwork/QNetworkReply>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(AIChatControllerLog)

class QAudioSource;
class QBuffer;
class QMediaCaptureSession;
class QMediaRecorder;
class QNetworkAccessManager;
class QmlObjectListModel;
class QTimer;
class Vehicle;

/// Controller for AI-powered chat interface that allows natural language vehicle control
class AIChatController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(QmlObjectListModel* messages READ messages CONSTANT)
    Q_PROPERTY(bool isProcessing READ isProcessing NOTIFY isProcessingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool isListening READ isListening NOTIFY isListeningChanged)
    Q_PROPERTY(bool voiceInputAvailable READ voiceInputAvailable NOTIFY voiceInputAvailableChanged)
    Q_PROPERTY(QString recognizedText READ recognizedText NOTIFY recognizedTextChanged)
    Q_PROPERTY(QmlObjectListModel* pois READ pois CONSTANT)

public:
    explicit AIChatController(Vehicle* vehicle, QObject* parent = nullptr);
    ~AIChatController();

    /// Message roles for the chat
    enum class MessageRole {
        User,
        Assistant,
        System
    };
    Q_ENUM(MessageRole)

    /// Command status for tracking execution
    enum class CommandStatus {
        None,
        Pending,
        RequiresConfirmation,
        Executed,
        Failed
    };
    Q_ENUM(CommandStatus)

    // Property accessors
    QmlObjectListModel* messages() const { return _messages; }
    QmlObjectListModel* pois() const { return _pois; }
    bool isProcessing() const { return _isProcessing; }
    QString errorMessage() const { return _errorMessage; }
    bool isListening() const { return _isListening; }
    bool voiceInputAvailable() const;
    QString recognizedText() const { return _recognizedText; }

    /// Send a user message to the AI
    Q_INVOKABLE void sendMessage(const QString& userMessage);

    /// Execute a command from a specific message index
    /// @param userInitiated true if user clicked the execute button (plays voice feedback)
    Q_INVOKABLE void executeCommand(int messageIndex, bool userInitiated = true);

    /// Clear all chat history
    Q_INVOKABLE void clearHistory();

    /// Start listening for voice input
    Q_INVOKABLE void startListening();

    /// Stop listening for voice input
    Q_INVOKABLE void stopListening();

    /// Add a POI at the given coordinate
    Q_INVOKABLE void addPOI(double latitude, double longitude);

    /// Remove a POI by its index in the list
    Q_INVOKABLE void removePOI(int index);

signals:
    void isProcessingChanged();
    void errorMessageChanged();
    void isListeningChanged();
    void recognizedTextChanged();
    void voiceInputAvailableChanged();
    void voiceInputReceived(const QString& text);

private slots:
    void _onNetworkReplyFinished();
    void _onVoiceInputReceived(const QString& text);

private:
    void _sendToClaudeAPI(const QString& userMessage);
    QString _buildDynamicContext() const;
    QString _getVehicleStateContext() const;
    void _processAIResponse(const QByteArray& responseData);
    void _addMessage(MessageRole role, const QString& content, const QString& action = QString(),
                     const QVariantMap& parameters = QVariantMap(), bool requiresConfirmation = false);
    void _addMessage(MessageRole role, const QString& content, const QVariantList& actions,
                     bool requiresConfirmation = false);
    bool _executeVehicleCommand(const QString& action, const QVariantMap& parameters);

    // Dangerous commands that require confirmation
    static bool _isDangerousCommand(const QString& action);

    // Text-to-speech for AI responses
    void _speakMessage(const QString& text);

    Vehicle* _vehicle = nullptr;
    QNetworkAccessManager* _networkManager = nullptr;
    QNetworkReply* _pendingReply = nullptr;
    QmlObjectListModel* _messages = nullptr;
    QmlObjectListModel* _pois = nullptr;
    bool _isProcessing = false;
    bool _isListening = false;
    QString _errorMessage;
    QString _recognizedText;
    bool _lastMessageWasVoice = false;

    // Action queue for sequential execution
    struct QueuedAction {
        QString action;
        QVariantMap parameters;
        QGeoCoordinate targetPosition;  // For position-based completion
        double targetAltitude = 0;       // For altitude-based completion
    };
    QList<QueuedAction> _actionQueue;
    QTimer* _actionQueueTimer = nullptr;
    QTimer* _asyncWaitTimer = nullptr;   // Timer for async_wait action
    int _currentMessageIndex = -1;      // Track which message owns the current queue
    bool _isExecutingQueue = false;

    void _processActionQueue();
    void _executeNextAction();
    bool _isCurrentActionComplete();
    void _clearActionQueue();
    void _onAsyncWaitComplete();


    // Voice input via Whisper API
    void _startAudioRecording();
    void _stopAudioRecording();
    void _sendAudioToWhisper();
    void _onWhisperReplyFinished();

    QMediaCaptureSession* _captureSession = nullptr;
    QMediaRecorder* _mediaRecorder = nullptr;
    QString _audioFilePath;
    QNetworkReply* _whisperReply = nullptr;
};

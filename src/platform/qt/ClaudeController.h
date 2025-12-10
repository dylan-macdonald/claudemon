/* Copyright (c) 2024 Claudemon Project
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPixmap>
#include <QJsonValue>
#include <QStandardPaths>
#include <QFile>

namespace QGBA {

class CoreController;
class InputController;

struct ClaudeInput {
    QString button;
    int count = 1;
    QString reasoning;
};

struct InputHistoryEntry {
    QString timestamp;
    QString input;
};

struct ClaudeNote {
    int id;
    QString timestamp;
    QString content;
};

class ClaudeController : public QObject {
Q_OBJECT

public:
    ClaudeController(QObject* parent = nullptr);
    ~ClaudeController();

    void setApiKey(const QString& key);
    QString apiKey() const { return m_apiKey; }
    void setCoreController(CoreController* controller);
    void setInputController(InputController* controller);
    void notifyGameStarted();
    void notifyGameStopped();

    enum Model {
        ModelOpus,
        ModelSonnet,
        ModelHaiku
    };

    void setModel(Model model);
    Model model() const { return m_model; }

    void setThinkingEnabled(bool enabled);
    bool thinkingEnabled() const { return m_thinkingEnabled; }

    void startGameLoop();
    void stopGameLoop();
    bool isRunning() const { return m_running; }
    bool canStart() const;
    
    QString getLastResponse() const { return m_lastResponse; }
    QList<ClaudeInput> getLastInputs() const { return m_lastInputs; }
    
    // Notes system
    QList<ClaudeNote> getNotes() const { return m_claudeNotes; }
    
    // Error state
    QString getLastError() const { return m_lastError; }
    int getConsecutiveErrors() const { return m_consecutiveErrors; }

signals:
    void responseReceived(const QString& response);
    void inputsGenerated(const QList<ClaudeInput>& inputs);
    void notesChanged();
    void errorOccurred(const QString& error);
    void criticalError(const QString& error, const QString& errorCode);
    void loopTick();
    void gameReadyChanged(bool ready);

private slots:
    void captureAndSendScreenshot();
    void handleApiResponse();
    void processInputs(const QList<ClaudeInput>& inputs);
    void onRequestTimeout();

private:
    QByteArray captureScreenshotData();
    QString parseInputsFromResponse(const QString& response, QList<ClaudeInput>& inputs);
    void parseNotesFromResponse(const QString& response);
    void addNote(const QString& content);
    void clearNote(int noteId);
    void clearAllNotes();
    void sendInputToGame(const QString& button, int count);
    int getGBAKeyCode(const QString& button);
    void handleCriticalError(const QString& error, const QString& errorCode);
    void saveGameState();
    void resetBackoff();
    int calculateBackoffMs() const;
    QString modelAlias() const;
    void saveSessionToDisk();
    void loadSessionFromDisk();
    bool isDirectionalButton(const QString& button) const;
    
    QNetworkAccessManager* m_networkManager;
    QTimer* m_gameLoopTimer;
    QTimer* m_requestTimeoutTimer;
    QTimer* m_inputPacingTimer;
    
    CoreController* m_coreController;
    InputController* m_inputController;
    bool m_coreReady = false;
    
    QString m_apiKey;
    bool m_running;
    bool m_requestInFlight;
    bool m_modelLocked;
    
    QString m_lastResponse;
    QList<ClaudeInput> m_lastInputs;
    QString m_lastError;
    
    // Backoff state
    int m_consecutiveErrors;
    int m_backoffMultiplier;

    // Session state
    Model m_model;
    bool m_thinkingEnabled;
    QJsonArray m_conversationMessages;
    QList<InputHistoryEntry> m_recentInputs;
    QList<ClaudeNote> m_claudeNotes;
    int m_nextNoteId;
    
    struct PendingInput {
        int keyCode;
        int remainingCount;
        bool isDirectional;
        int originalCount; // for directional hold duration calculation
    };
    
    // Pending inputs for paced delivery
    QList<PendingInput> m_pendingInputs;
    int m_currentKey;
    bool m_currentKeyIsDirectional;
    
    static const QString CLAUDE_API_URL;
    static const int LOOP_INTERVAL_MS = 2000;      // 2 seconds between actions
    static const int REQUEST_TIMEOUT_MS = 30000;   // 30 second timeout
    static const int MAX_CONSECUTIVE_ERRORS = 3;   // Stop after 3 consecutive errors
    static const int BASE_BACKOFF_MS = 1000;       // 1 second base backoff
    static const int MAX_BACKOFF_MS = 30000;       // 30 second max backoff
    static const int MAX_INPUT_COUNT = 10;         // Cap repeated inputs
    static const int INPUT_PACING_MS = 50;         // 50ms between button presses
    static const int DIRECTION_HOLD_MS = 150;      // ~3 frames for directional input
};

}

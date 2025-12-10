/* Copyright (c) 2024 Claudemon Project
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClaudeController.h"
#include "CoreController.h"
#include "InputController.h"

#include <QBuffer>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QRegularExpression>
#include <QDebug>
#include <QPixmap>
#include <QImage>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QUrl>

#include <mgba/core/input.h>
#include <mgba/internal/gba/input.h>
#include <mgba/core/core.h>
#include <mgba/core/thread.h>

using namespace QGBA;

const QString ClaudeController::CLAUDE_API_URL = "https://api.anthropic.com/v1/messages";

ClaudeController::ClaudeController(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_gameLoopTimer(new QTimer(this))
    , m_requestTimeoutTimer(new QTimer(this))
    , m_inputPacingTimer(new QTimer(this))
    , m_coreController(nullptr)
    , m_inputController(nullptr)
    , m_running(false)
    , m_requestInFlight(false)
    , m_modelLocked(false)
    , m_consecutiveErrors(0)
    , m_backoffMultiplier(1)
    , m_model(ModelSonnet)
    , m_thinkingEnabled(false)
    , m_nextNoteId(1)
    , m_currentKey(-1)
    , m_currentKeyIsDirectional(false)
{
    m_gameLoopTimer->setSingleShot(false);
    m_gameLoopTimer->setInterval(LOOP_INTERVAL_MS);
    
    m_requestTimeoutTimer->setSingleShot(true);
    m_requestTimeoutTimer->setInterval(REQUEST_TIMEOUT_MS);
    
    m_inputPacingTimer->setSingleShot(true);
    m_inputPacingTimer->setInterval(INPUT_PACING_MS);
    
    connect(m_gameLoopTimer, &QTimer::timeout, this, &ClaudeController::captureAndSendScreenshot);
    connect(m_requestTimeoutTimer, &QTimer::timeout, this, &ClaudeController::onRequestTimeout);
    connect(m_inputPacingTimer, &QTimer::timeout, this, [this]() {
        // Release the currently pressed key, then schedule the next press if any.
        if (m_coreController && m_currentKey >= 0) {
            m_coreController->clearKey(m_currentKey);
        }
        m_currentKey = -1;
        m_currentKeyIsDirectional = false;

        if (!m_pendingInputs.isEmpty() && m_coreController && m_running) {
            auto& front = m_pendingInputs.front();
            m_coreController->addKey(front.keyCode);
            m_currentKey = front.keyCode;
            m_currentKeyIsDirectional = front.isDirectional;
            front.remainingCount--;
            if (front.remainingCount <= 0) {
                m_pendingInputs.removeFirst();
            }
            
            // Set timer interval based on input type
            if (front.isDirectional && front.originalCount > 1) {
                // Hold directional input for longer
                m_inputPacingTimer->setInterval(DIRECTION_HOLD_MS * front.originalCount);
            } else {
                // Normal pacing for buttons and single directional taps
                m_inputPacingTimer->setInterval(INPUT_PACING_MS);
            }
            
            m_inputPacingTimer->start();
        }
    });

    loadSessionFromDisk();
}

ClaudeController::~ClaudeController() {
    stopGameLoop();
}

void ClaudeController::setApiKey(const QString& key) {
    m_apiKey = key;
    saveSessionToDisk(); // persist key for next launch
}

void ClaudeController::setCoreController(CoreController* controller) {
    m_coreController = controller;
    m_coreReady = controller != nullptr;
    emit gameReadyChanged(canStart());
}

void ClaudeController::setInputController(InputController* controller) {
    m_inputController = controller;
}

void ClaudeController::notifyGameStarted() {
    emit gameReadyChanged(canStart());
}

void ClaudeController::notifyGameStopped() {
    if (m_running) {
        stopGameLoop();
    }
    emit gameReadyChanged(canStart());
}

bool ClaudeController::canStart() const {
    if (!m_coreReady || !m_inputController) {
        return false;
    }
    // Require an active core (ROM loaded)
    auto thread = m_coreController ? m_coreController->thread() : nullptr;
    return thread && thread->core;
}

void ClaudeController::setModel(Model model) {
    if (m_modelLocked) return;
    m_model = model;
}

void ClaudeController::setThinkingEnabled(bool enabled) {
    if (m_modelLocked) return;
    m_thinkingEnabled = enabled;
}

void ClaudeController::startGameLoop() {
    if (m_apiKey.isEmpty()) {
        emit errorOccurred("API key is required");
        return;
    }
    if (!m_coreController) {
        emit errorOccurred("No game loaded - please load a ROM first");
        return;
    }
    if (!m_inputController) {
        emit errorOccurred("Input controller not available");
        return;
    }
    
    resetBackoff();
    m_running = true;
    m_requestInFlight = false;
    m_modelLocked = true;
    m_gameLoopTimer->start();
    qDebug() << "Claude game loop started";
}

void ClaudeController::stopGameLoop() {
    m_running = false;
    m_requestInFlight = false;
    m_modelLocked = false;
    m_gameLoopTimer->stop();
    m_requestTimeoutTimer->stop();
    m_inputPacingTimer->stop();
    m_pendingInputs.clear();
    if (m_coreController && m_currentKey >= 0) {
        m_coreController->clearKey(m_currentKey);
    }
    m_currentKey = -1;
    qDebug() << "Claude game loop stopped";
}

void ClaudeController::resetBackoff() {
    m_consecutiveErrors = 0;
    m_backoffMultiplier = 1;
    m_lastError.clear();
}

int ClaudeController::calculateBackoffMs() const {
    int backoff = BASE_BACKOFF_MS * m_backoffMultiplier;
    return qMin(backoff, MAX_BACKOFF_MS);
}

QString ClaudeController::modelAlias() const {
    switch (m_model) {
        case ModelOpus: return "claude-opus-4-5-20251101";
        case ModelHaiku: return "claude-haiku-4-5-20251001";
        case ModelSonnet:
        default: return "claude-sonnet-4-5-20250929";
    }
}

QByteArray ClaudeController::captureScreenshotData() {
    if (!m_coreController) {
        qDebug() << "captureScreenshotData: no core controller";
        return QByteArray();
    }
    
    // Use CoreController's getPixels() which properly handles the framebuffer
    QImage image = m_coreController->getPixels();
    
    if (image.isNull() || image.width() == 0 || image.height() == 0) {
        qDebug() << "captureScreenshotData: got null or empty image";
        return QByteArray();
    }
    
    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    
    // Convert to RGB888 for Claude API (remove alpha channel, ensure consistent format)
    QImage rgb888 = image.convertToFormat(QImage::Format_RGB888);
    
    // Save as PNG
    if (!rgb888.save(&buffer, "PNG")) {
        qDebug() << "captureScreenshotData: failed to save PNG";
        return QByteArray();
    }
    
    qDebug() << "Captured screenshot:" << image.width() << "x" << image.height() 
             << "pixels, size:" << imageData.size() << "bytes";
    return imageData;
}

void ClaudeController::captureAndSendScreenshot() {
    if (!m_coreController || !m_running) {
        return;
    }
    
    // Guard against concurrent requests
    if (m_requestInFlight) {
        qDebug() << "Request already in flight, skipping this tick";
        return;
    }
    
    // Capture actual screenshot data
    QByteArray imageData = captureScreenshotData();
    
    if (imageData.isEmpty()) {
        emit errorOccurred("Failed to capture screenshot");
        return;
    }
    
    QJsonObject requestBody;
    requestBody["model"] = modelAlias();
    requestBody["max_tokens"] = 300;
    if (m_thinkingEnabled) {
        // Hint to allow thinking (metadata only; still enforce command-only output)
        requestBody["metadata"] = QJsonObject{{"thinking", true}};
    }

    QJsonArray messages;
    // prepend existing history (text only)
    for (const auto& msgVal : m_conversationMessages) {
        messages.append(msgVal);
    }
    QJsonObject message;
    message["role"] = "user";
    
    QJsonArray content;
    QJsonObject textContent;
    textContent["type"] = "text";
    
    QString promptText = "You are playing Pokemon Emerald on a Game Boy Advance emulator. "
                         "Look at this screenshot and decide what button to press next. "
                         "Your goal is to make progress in the game. "
                         "Respond with ONLY a simple button command like 'a', 'start', 'up 3', 'b 2', etc. "
                         "Valid buttons: a, b, start, select, up, down, left, right, l, r. "
                         "You can add a number after for repeats (max 10). "
                         "Do not include any other text or explanation. "
                         "Do not include reasoning text; only the button command.\n\n";
    
    // Add input history if we have any
    if (!m_recentInputs.isEmpty()) {
        promptText += "Recent inputs (to avoid repetition):\n";
        for (const auto& inputEntry : m_recentInputs) {
            promptText += QString("%1: %2\n").arg(inputEntry.timestamp, inputEntry.input);
        }
        promptText += "\n";
    }
    
    // Add notes if we have any
    if (!m_claudeNotes.isEmpty()) {
        promptText += "Your notes:\n";
        for (const auto& note : m_claudeNotes) {
            promptText += QString("[%1] Note #%2: %3\n").arg(note.timestamp).arg(note.id).arg(note.content);
        }
        promptText += "You can add notes with [NOTE: message], clear specific notes with [CLEAR NOTE: #], or clear all with [CLEAR ALL NOTES].\n\n";
    }
    
    textContent["text"] = promptText;
    content.append(textContent);
    
    // Add image data
    QJsonObject imageContent;
    imageContent["type"] = "image";
    
    QJsonObject source;
    source["type"] = "base64";
    source["media_type"] = "image/png";
    source["data"] = QString(imageData.toBase64());
    
    imageContent["source"] = source;
    content.append(imageContent);
    
    message["content"] = content;
    messages.append(message);

    // Also push this user message into history (text only, keep last 10)
    QJsonObject userHist;
    userHist["role"] = "user";
    QJsonArray userContent;
    QJsonObject userText;
    userText["type"] = "text";
    userText["text"] = textContent["text"];
    userContent.append(userText);
    userHist["content"] = userContent;
    m_conversationMessages.append(userHist);
    while (m_conversationMessages.size() > 10) {
        m_conversationMessages.removeFirst();
    }
    requestBody["messages"] = messages;
    
    QJsonDocument doc(requestBody);
    QByteArray data = doc.toJson();
    
    // Debug: Log request info (mask API key)
    QString maskedKey = m_apiKey.left(8) + "..." + m_apiKey.right(4);
    qDebug() << "Sending API request to:" << CLAUDE_API_URL;
    qDebug() << "Model:" << modelAlias();
    qDebug() << "API Key (masked):" << maskedKey;
    qDebug() << "Request size:" << data.size() << "bytes";
    
    QNetworkRequest netRequest{QUrl(CLAUDE_API_URL)};
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    netRequest.setRawHeader("x-api-key", m_apiKey.toUtf8());
    netRequest.setRawHeader("anthropic-version", "2023-06-01");
    netRequest.setRawHeader("User-Agent", "Claudemon/1.0 (mGBA fork)");
    
    // Set transfer timeout
    netRequest.setTransferTimeout(REQUEST_TIMEOUT_MS);
    
    m_requestInFlight = true;
    m_requestTimeoutTimer->start();
    
    QNetworkReply* reply = m_networkManager->post(netRequest, data);
    connect(reply, &QNetworkReply::finished, this, &ClaudeController::handleApiResponse);
    
    emit loopTick();
}

void ClaudeController::onRequestTimeout() {
    if (!m_requestInFlight) return;
    
    qDebug() << "Request timed out";
    m_requestInFlight = false;
    
    m_consecutiveErrors++;
    m_backoffMultiplier *= 2;
    m_lastError = "Request timed out";
    
    if (m_consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
        handleCriticalError("Request timed out after multiple attempts", "TIMEOUT");
    } else {
        emit errorOccurred(QString("Request timed out (attempt %1/%2, retrying in %3s)")
            .arg(m_consecutiveErrors)
            .arg(MAX_CONSECUTIVE_ERRORS)
            .arg(calculateBackoffMs() / 1000.0, 0, 'f', 1));
        
        // Apply backoff by adjusting next timer interval
        m_gameLoopTimer->setInterval(LOOP_INTERVAL_MS + calculateBackoffMs());
    }
}

void ClaudeController::handleApiResponse() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    m_requestInFlight = false;
    m_requestTimeoutTimer->stop();
    reply->deleteLater();
    
    // Always read the response body - it may contain error details even on HTTP errors
    QByteArray responseData = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString httpReason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    
    qDebug() << "API Response - HTTP" << httpStatus << httpReason;
    qDebug() << "Response body:" << responseData.left(500); // First 500 chars for debugging
    
    // Check for network errors
    if (reply->error() != QNetworkReply::NoError) {
        m_consecutiveErrors++;
        m_backoffMultiplier *= 2;
        
        // Try to parse error details from response body
        QString errorDetail;
        QJsonDocument errorDoc = QJsonDocument::fromJson(responseData);
        if (!errorDoc.isNull() && errorDoc.isObject()) {
            QJsonObject errorObj = errorDoc.object();
            if (errorObj.contains("error")) {
                QJsonObject err = errorObj["error"].toObject();
                QString errType = err["type"].toString();
                QString errMsg = err["message"].toString();
                errorDetail = QString("%1: %2").arg(errType, errMsg);
                
                // Check for critical API errors
                if (errType == "invalid_api_key" || errType == "authentication_error") {
                    m_lastError = errorDetail;
                    handleCriticalError(QString("API error: %1").arg(errorDetail), errType);
                    return;
                }
            }
        }
        
        if (errorDetail.isEmpty()) {
            errorDetail = QString("HTTP %1 %2 - %3").arg(httpStatus).arg(httpReason, reply->errorString());
        }
        m_lastError = errorDetail;
        
        if (m_consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
            handleCriticalError(QString("Network error: %1").arg(errorDetail), QString::number(httpStatus));
        } else {
            emit errorOccurred(QString("Network error: %1 (attempt %2/%3)")
                .arg(errorDetail)
                .arg(m_consecutiveErrors)
                .arg(MAX_CONSECUTIVE_ERRORS));
            
            // Apply backoff
            m_gameLoopTimer->setInterval(LOOP_INTERVAL_MS + calculateBackoffMs());
        }
        return;
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        m_consecutiveErrors++;
        m_lastError = "Invalid JSON response";
        
        if (m_consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
            handleCriticalError("API returned invalid JSON", "PARSE_ERROR");
        } else {
            emit errorOccurred(QString("Invalid JSON response: %1").arg(parseError.errorString()));
        }
        return;
    }
    
    QJsonObject response = doc.object();
    
    // Check for API errors
    if (response.contains("error")) {
        QJsonObject error = response["error"].toObject();
        QString errorType = error["type"].toString();
        QString errorMessage = error["message"].toString();
        
        m_consecutiveErrors++;
        m_backoffMultiplier *= 2;
        m_lastError = errorMessage;
        
        // Check for rate limiting or credit exhaustion
        bool isCritical = (errorType == "rate_limit_error" || 
                          errorType == "overloaded_error" ||
                          errorType == "insufficient_quota" ||
                          errorType == "invalid_api_key" ||
                          errorType == "authentication_error");
        
        if (isCritical || m_consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
            handleCriticalError(QString("API error: %1").arg(errorMessage), errorType);
        } else {
            emit errorOccurred(QString("API error: %1 (attempt %2/%3)")
                .arg(errorMessage)
                .arg(m_consecutiveErrors)
                .arg(MAX_CONSECUTIVE_ERRORS));
            
            // Apply backoff
            m_gameLoopTimer->setInterval(LOOP_INTERVAL_MS + calculateBackoffMs());
        }
        return;
    }
    
    // Success - reset backoff
    resetBackoff();
    m_gameLoopTimer->setInterval(LOOP_INTERVAL_MS);
    
    // Parse content
    if (response.contains("content")) {
        QJsonArray contentArray = response["content"].toArray();
        if (!contentArray.isEmpty()) {
            QJsonObject firstContent = contentArray[0].toObject();
            if (firstContent["type"].toString() == "text") {
                QString responseText = firstContent["text"].toString();
                m_lastResponse = responseText;
                
                QList<ClaudeInput> inputs;
                parseInputsFromResponse(responseText, inputs);
                parseNotesFromResponse(responseText);
                m_lastInputs = inputs;

                // Append assistant message to history (text only)
                QJsonObject assistantMsg;
                assistantMsg["role"] = "assistant";
                QJsonArray assistantContent;
                QJsonObject assistantText;
                assistantText["type"] = "text";
                assistantText["text"] = responseText;
                assistantContent.append(assistantText);
                assistantMsg["content"] = assistantContent;
                m_conversationMessages.append(assistantMsg);

                // Trim history to last 10 messages (assistant+user)
                while (m_conversationMessages.size() > 10) {
                    m_conversationMessages.removeFirst();
                }
                saveSessionToDisk();
                
                emit responseReceived(responseText);
                emit inputsGenerated(inputs);
                
                processInputs(inputs);
            } else {
                emit errorOccurred("Unexpected response format (no text content)");
            }
        } else {
            emit errorOccurred("Empty response content");
        }
    } else {
        emit errorOccurred("Response missing content field");
    }
}

void ClaudeController::handleCriticalError(const QString& error, const QString& errorCode) {
    qDebug() << "Critical error:" << error << "Code:" << errorCode;
    
    // Stop the game loop
    stopGameLoop();
    
    // Save game state
    saveGameState();
    
    // Emit signal for UI to handle
    emit criticalError(error, errorCode);
    
    // Show message box
    QString title = "Claude AI Stopped";
    QString message = QString(
        "Claude AI has stopped due to an error.\n\n"
        "Error: %1\n"
        "Error Code: %2\n\n"
        "Your game has been automatically saved.\n\n"
        "Please check:\n"
        "• Your API key is valid\n"
        "• You have available API credits\n"
        "• Your internet connection is working"
    ).arg(error).arg(errorCode);
    
    QMessageBox::critical(nullptr, title, message, QMessageBox::Ok);
}

void ClaudeController::saveGameState() {
    if (!m_coreController) {
        qDebug() << "Cannot save game state: no core controller";
        return;
    }
    
    // Use the core controller's save state functionality
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString saveName = QString("claude_autosave_%1").arg(timestamp);
    
    // Save to slot 9 (usually unused by players)
    m_coreController->saveState(9);
    
    qDebug() << "Game state saved to slot 9 (autosave due to error)";
}

QString ClaudeController::parseInputsFromResponse(const QString& response, QList<ClaudeInput>& inputs) {
    inputs.clear();
    
    // Simple parsing - look for button commands
    QString trimmed = response.trimmed().toLower();
    
    // Remove any punctuation and extra whitespace
    trimmed = trimmed.replace(QRegularExpression("[.,!?;:]"), " ");
    trimmed = trimmed.simplified();
    
    // Parse patterns like "up 3", "a", "start", "b 2"
    QRegularExpression re("\\b(up|down|left|right|a|b|l|r|start|select)(?:\\s+(\\d+))?\\b");
    QRegularExpressionMatchIterator it = re.globalMatch(trimmed);
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        ClaudeInput input;
        input.button = match.captured(1);
        int count = match.captured(2).isEmpty() ? 1 : match.captured(2).toInt();
        // Cap the count to prevent flooding
        input.count = qMin(count, MAX_INPUT_COUNT);
        input.reasoning = response;
        inputs.append(input);
    }
    
    // If no specific pattern found, try to extract just the first button name
    if (inputs.isEmpty()) {
        QStringList buttonNames = {"up", "down", "left", "right", "a", "b", "l", "r", "start", "select"};
        for (const QString& button : buttonNames) {
            if (trimmed.contains(button)) {
                ClaudeInput input;
                input.button = button;
                input.count = 1;
                input.reasoning = response;
                inputs.append(input);
                break; // Only take the first match
            }
        }
    }
    
    return response;
}

void ClaudeController::parseNotesFromResponse(const QString& response) {
    // Look for [NOTE: ...] commands
    QRegularExpression noteRegex("\\[NOTE:\\s*(.+?)\\]", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator noteIt = noteRegex.globalMatch(response);
    
    while (noteIt.hasNext()) {
        QRegularExpressionMatch match = noteIt.next();
        QString noteContent = match.captured(1).trimmed();
        if (!noteContent.isEmpty()) {
            addNote(noteContent);
        }
    }
    
    // Look for [CLEAR NOTE: X] commands
    QRegularExpression clearNoteRegex("\\[CLEAR\\s+NOTE:\\s*(\\d+)\\]", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator clearIt = clearNoteRegex.globalMatch(response);
    
    while (clearIt.hasNext()) {
        QRegularExpressionMatch match = clearIt.next();
        int noteId = match.captured(1).toInt();
        clearNote(noteId);
    }
    
    // Look for [CLEAR ALL NOTES] command
    if (response.contains(QRegularExpression("\\[CLEAR\\s+ALL\\s+NOTES\\]", QRegularExpression::CaseInsensitiveOption))) {
        clearAllNotes();
    }
}

void ClaudeController::addNote(const QString& content) {
    ClaudeNote note;
    note.id = m_nextNoteId++;
    note.timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    note.content = content;
    
    m_claudeNotes.append(note);
    
    // Keep only last 10 notes (FIFO)
    while (m_claudeNotes.size() > 10) {
        m_claudeNotes.removeFirst();
    }
    
    emit notesChanged();
    saveSessionToDisk(); // Persist notes
}

void ClaudeController::clearNote(int noteId) {
    for (int i = 0; i < m_claudeNotes.size(); ++i) {
        if (m_claudeNotes[i].id == noteId) {
            m_claudeNotes.removeAt(i);
            emit notesChanged();
            saveSessionToDisk();
            break;
        }
    }
}

void ClaudeController::clearAllNotes() {
    if (!m_claudeNotes.isEmpty()) {
        m_claudeNotes.clear();
        emit notesChanged();
        saveSessionToDisk();
    }
}

void ClaudeController::processInputs(const QList<ClaudeInput>& inputs) {
    if (!m_inputController || !m_running) return;
    
    // Clear any pending inputs from previous call
    m_pendingInputs.clear();
    
    // Record inputs to history
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    for (const ClaudeInput& input : inputs) {
        int keyCode = getGBAKeyCode(input.button);
        if (keyCode >= 0) {
            PendingInput pendingInput;
            pendingInput.keyCode = keyCode;
            pendingInput.isDirectional = isDirectionalButton(input.button);
            pendingInput.originalCount = input.count;
            
            // For directional inputs with count > 1, treat as a single hold rather than multiple taps
            if (pendingInput.isDirectional && input.count > 1) {
                pendingInput.remainingCount = 1; // Will be held for longer duration
                qDebug() << "Queued directional hold:" << input.button << "for" << input.count << "units";
            } else {
                pendingInput.remainingCount = input.count; // Normal behavior for buttons and single directional taps
                qDebug() << "Queued input:" << input.button << "x" << input.count;
            }
            
            m_pendingInputs.append(pendingInput);
            
            // Add to input history
            InputHistoryEntry entry;
            entry.timestamp = timestamp;
            entry.input = input.count > 1 ? QString("%1 x%2").arg(input.button).arg(input.count) : input.button;
            m_recentInputs.append(entry);
        } else {
            qDebug() << "Unknown button:" << input.button;
        }
    }
    
    // Keep only last 15 inputs
    while (m_recentInputs.size() > 15) {
        m_recentInputs.removeFirst();
    }
    
    // Start processing the queue
    if (!m_pendingInputs.isEmpty() && !m_inputPacingTimer->isActive()) {
        // Send first input immediately
        auto& front = m_pendingInputs.front();
        if (m_coreController && m_running) {
            m_coreController->addKey(front.keyCode);
            m_currentKey = front.keyCode;
            m_currentKeyIsDirectional = front.isDirectional;
            front.remainingCount--;
            if (front.remainingCount <= 0) {
                m_pendingInputs.removeFirst();
            }
            
            // Set timer interval based on input type
            if (front.isDirectional && front.originalCount > 1) {
                // Hold directional input for longer
                m_inputPacingTimer->setInterval(DIRECTION_HOLD_MS * front.originalCount);
            } else {
                // Normal pacing for buttons and single directional taps
                m_inputPacingTimer->setInterval(INPUT_PACING_MS);
            }
            
            m_inputPacingTimer->start();
        }
    }
}

void ClaudeController::sendInputToGame(const QString& button, int count) {
    if (!m_inputController) return;
    
    int keyCode = getGBAKeyCode(button);
    if (keyCode < 0) {
        qDebug() << "Unknown button:" << button;
        return;
    }
    
    // Cap the count
    count = qMin(count, MAX_INPUT_COUNT);
    
    qDebug() << "Sending input:" << button << "x" << count;
    
    // Queue for paced delivery instead of immediate flood
    PendingInput pendingInput;
    pendingInput.keyCode = keyCode;
    pendingInput.isDirectional = isDirectionalButton(button);
    pendingInput.originalCount = count;
    
    if (pendingInput.isDirectional && count > 1) {
        pendingInput.remainingCount = 1; // Will be held for longer duration
    } else {
        pendingInput.remainingCount = count;
    }
    
    m_pendingInputs.append(pendingInput);
    
    if (!m_inputPacingTimer->isActive()) {
        auto& front = m_pendingInputs.front();
        if (m_coreController && m_running) {
            m_coreController->addKey(front.keyCode);
            m_currentKey = front.keyCode;
            m_currentKeyIsDirectional = front.isDirectional;
            front.remainingCount--;
            if (front.remainingCount <= 0) {
                m_pendingInputs.removeFirst();
            }
            
            // Set timer interval based on input type
            if (front.isDirectional && front.originalCount > 1) {
                m_inputPacingTimer->setInterval(DIRECTION_HOLD_MS * front.originalCount);
            } else {
                m_inputPacingTimer->setInterval(INPUT_PACING_MS);
            }
            
            if (!m_pendingInputs.isEmpty()) {
                m_inputPacingTimer->start();
            } else {
                // Still release the pressed key after the pacing interval
                m_inputPacingTimer->start();
            }
        }
    }
}

int ClaudeController::getGBAKeyCode(const QString& button) {
    QString lower = button.toLower();
    
    if (lower == "a") return GBA_KEY_A;
    if (lower == "b") return GBA_KEY_B;
    if (lower == "l") return GBA_KEY_L;
    if (lower == "r") return GBA_KEY_R;
    if (lower == "start") return GBA_KEY_START;
    if (lower == "select") return GBA_KEY_SELECT;
    if (lower == "up") return GBA_KEY_UP;
    if (lower == "down") return GBA_KEY_DOWN;
    if (lower == "left") return GBA_KEY_LEFT;
    if (lower == "right") return GBA_KEY_RIGHT;
    
    return -1; // Unknown button
}

bool ClaudeController::isDirectionalButton(const QString& button) const {
    QString lower = button.toLower();
    return (lower == "up" || lower == "down" || lower == "left" || lower == "right");
}

void ClaudeController::saveSessionToDisk() {
    QJsonObject root;
    root["model"] = modelAlias();
    root["apiKey"] = m_apiKey;
    root["thinking"] = m_thinkingEnabled;
    root["history"] = m_conversationMessages;
    root["nextNoteId"] = m_nextNoteId;
    
    // Save notes
    QJsonArray notesArray;
    for (const ClaudeNote& note : m_claudeNotes) {
        QJsonObject noteObj;
        noteObj["id"] = note.id;
        noteObj["timestamp"] = note.timestamp;
        noteObj["content"] = note.content;
        notesArray.append(noteObj);
    }
    root["notes"] = notesArray;

    QJsonDocument doc(root);
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    QDir().mkpath(basePath);
    QFile f(basePath + "/claude_session.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(doc.toJson());
        f.close();
    }
}

void ClaudeController::loadSessionFromDisk() {
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    QFile f(basePath + "/claude_session.json");
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return;
    }
    QByteArray data = f.readAll();
    f.close();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }
    QJsonObject root = doc.object();
    QString modelStr = root["model"].toString();
    // Handle both old and new model name formats
    if (modelStr.contains("opus", Qt::CaseInsensitive)) m_model = ModelOpus;
    else if (modelStr.contains("haiku", Qt::CaseInsensitive)) m_model = ModelHaiku;
    else m_model = ModelSonnet;
    if (root.contains("apiKey")) {
        m_apiKey = root["apiKey"].toString();
    }
    m_thinkingEnabled = root["thinking"].toBool(false);
    if (root.contains("history") && root["history"].isArray()) {
        m_conversationMessages = root["history"].toArray();
        while (m_conversationMessages.size() > 10) {
            m_conversationMessages.removeFirst();
        }
    }
    
    // Load notes
    m_nextNoteId = root["nextNoteId"].toInt(1);
    if (root.contains("notes") && root["notes"].isArray()) {
        m_claudeNotes.clear();
        QJsonArray notesArray = root["notes"].toArray();
        for (const QJsonValue& noteVal : notesArray) {
            if (noteVal.isObject()) {
                QJsonObject noteObj = noteVal.toObject();
                ClaudeNote note;
                note.id = noteObj["id"].toInt();
                note.timestamp = noteObj["timestamp"].toString();
                note.content = noteObj["content"].toString();
                m_claudeNotes.append(note);
            }
        }
    }
}

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
const QString ClaudeController::GAME_STATE_PATH = "scripts/game_state.json";

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
    , m_webSearchEnabled(false)
    , m_nextNoteId(1)
    , m_lastKnownX(-1)
    , m_lastKnownY(-1)
    , m_hasKnownPosition(false)
    , m_turnCounter(0)
    , m_positionBeforeX(-1)
    , m_positionBeforeY(-1)
    , m_hasPositionBefore(false)
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
        // Release the currently pressed key, then process next if any
        if (m_coreController && m_currentKey >= 0) {
            m_coreController->clearKey(m_currentKey);
        }
        m_currentKey = -1;
        m_currentKeyIsDirectional = false;

        processNextPendingInput();
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

void ClaudeController::setWebSearchEnabled(bool enabled) {
    if (m_modelLocked) return;
    m_webSearchEnabled = enabled;
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

    // Convert to RGB888 first (remove alpha channel, ensure consistent format)
    QImage rgb888 = image.convertToFormat(QImage::Format_RGB888);

    // Upscale to 1280x720 for better vision model clarity
    // GBA native: 240x160 (3:2 aspect ratio)
    // Target: 1280x720 (16:9 aspect ratio)
    // Strategy: Scale to 1080x720 (maintains 3:2 ratio), then letterbox to 1280x720

    // Scale to 1080x720 using nearest neighbor (preserves pixel art edges)
    QImage scaled = rgb888.scaled(
        1080, 720,
        Qt::IgnoreAspectRatio,  // We're already calculating the correct size
        Qt::FastTransformation  // Nearest neighbor - CRITICAL for pixel art
    );

    // Ensure scaled image is RGB888 (scaled() should preserve format, but verify)
    if (scaled.format() != QImage::Format_RGB888) {
        scaled = scaled.convertToFormat(QImage::Format_RGB888);
    }

    // Create 1280x720 canvas with black letterbox bars
    QImage final(1280, 720, QImage::Format_RGB888);
    final.fill(Qt::black);

    // Center the scaled image (100px bars on left and right)
    int offsetX = (1280 - 1080) / 2;  // 100px
    int offsetY = 0;

    // Copy scaled image to center of canvas
    for (int y = 0; y < scaled.height(); ++y) {
        memcpy(
            final.scanLine(y + offsetY) + (offsetX * 3),  // 3 bytes per pixel (RGB888)
            scaled.scanLine(y),
            scaled.width() * 3
        );
    }

    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);

    // Save as PNG
    if (!final.save(&buffer, "PNG")) {
        qDebug() << "captureScreenshotData: failed to save PNG";
        return QByteArray();
    }

    qDebug() << "Captured screenshot: Original" << image.width() << "x" << image.height()
             << "-> Upscaled" << final.width() << "x" << final.height()
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

    // VERIFICATION SYSTEM: Capture position BEFORE taking the screenshot
    // This will be our "position before" for the turn record
    m_hasPositionBefore = m_hasKnownPosition;
    m_positionBeforeX = m_lastKnownX;
    m_positionBeforeY = m_lastKnownY;

    // Validate notes against ground truth at the start of each turn
    validateNotesAgainstGroundTruth();

    // Capture actual screenshot data
    QByteArray currentScreenshot = captureScreenshotData();

    if (currentScreenshot.isEmpty()) {
        emit errorOccurred("Failed to capture screenshot");
        return;
    }
    
    QJsonObject requestBody;
    requestBody["model"] = modelAlias();
    
    // Set appropriate max_tokens based on thinking
    if (m_thinkingEnabled) {
        requestBody["max_tokens"] = 16000;  // Budget for thinking + response
        QJsonObject thinking;
        thinking["type"] = "enabled";
        thinking["budget_tokens"] = 10000;
        requestBody["thinking"] = thinking;
    } else {
        requestBody["max_tokens"] = 300;
    }
    
    // Add web search tool if enabled
    if (m_webSearchEnabled) {
        QJsonArray tools;
        QJsonObject webSearchTool;
        webSearchTool["type"] = "web_search_20250305";
        webSearchTool["name"] = "web_search";
        webSearchTool["max_uses"] = 3;
        tools.append(webSearchTool);
        requestBody["tools"] = tools;
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
    
    QString promptText = "You are Claude, playing Pokemon Emerald. Goal: Become the Pokemon Champion!\n\n"
                         "## VISUAL CONTEXT\n"
                         "You are viewing PIXEL ART screenshots from a Game Boy Advance game.\n"
                         "- Original resolution: 240x160 pixels (upscaled to 1080x720 for clarity, letterboxed to 1280x720)\n"
                         "- Text appears in pixel font in a dialogue box at the bottom of the screen\n"
                         "- Characters and objects are small sprites (16x16 to 32x32 pixels typically)\n"
                         "- Colors are limited (GBA palette)\n"
                         "- Black bars on left and right are letterboxing (not part of the game)\n\n"
                         "IMPORTANT:\n"
                         "- READ THE ACTUAL PIXELS. Don't assume or fill in details you can't see clearly.\n"
                         "- If you can't read text clearly, say so rather than guessing.\n"
                         "- Sprite details are minimal - a few pixels difference distinguishes characters.\n"
                         "- The dialogue box at the bottom contains the most important text to read.\n\n"
                         "## READING THE SCREEN\n"
                         "When describing what you see:\n"
                         "1. DIALOGUE BOX (bottom): Read the exact text. If unclear, say \"text unclear\" rather than guessing.\n"
                         "2. SPEAKER: Who is talking? Look for name labels or context.\n"
                         "3. SCENE: Where are you? Indoor/outdoor, what room, what's visible.\n"
                         "4. SPRITES: What characters/objects are on screen? Describe positions.\n"
                         "5. UI ELEMENTS: Any menus, health bars, indicators?\n\n"
                         "If the screen is a menu:\n"
                         "- What options are listed?\n"
                         "- Which option is highlighted/selected (usually indicated by arrow or color)?\n"
                         "- What buttons are shown at the bottom?\n\n"
                         "## CORE RULE: VERIFY, DON'T PREDICT\n"
                         "You tend to confuse INTENTIONS with RESULTS. Never claim an action worked until you SEE proof in the next screenshot.\n"
                         "- WRONG: Press down -> [NOTE: I'm downstairs now] (you haven't verified!)\n"
                         "- RIGHT: Press down -> (next turn) see new room -> [NOTE: Made it downstairs]\n\n"
                         "## NOTE TIMING\n"
                         "Each turn you see the RESULT of your PREVIOUS action and CHOOSE your NEXT action.\n"
                         "- Write notes about PREVIOUS action results (you have evidence)\n"
                         "- NEVER write notes about CURRENT action outcomes (result is in the future)\n\n"
                         "## INPUTS\n"
                         "Buttons: a, b, start, select, up, down, left, right, l, r\n"
                         "Hold: \"up 3\" | Chain: \"up 2 right a\"\n\n"
                         "## NOTES (Use Sparingly)\n"
                         "Notes persist between turns. They're for IMPORTANT things you need to remember, not a turn-by-turn diary.\n\n"
                         "Commands:\n"
                         "[NOTE: message] - save a note\n"
                         "[CLEAR NOTE: 3] - delete note #3\n"
                         "[CLEAR ALL NOTES] - clear all\n\n"
                         "WHEN TO WRITE A NOTE:\n"
                         "- You discovered something important (item location, NPC hint, puzzle solution)\n"
                         "- You need to remember an objective across multiple turns\n"
                         "- You tried something that failed and must not repeat it\n"
                         "- Information you'd forget but need later\n\n"
                         "WHEN NOT TO WRITE A NOTE:\n"
                         "- Routine actions (pressed A, dialogue advanced)\n"
                         "- Turn-by-turn narration\n"
                         "- Things visible in the current screenshot\n"
                         "- Things already in your notes\n\n"
                         "BAD (note every turn):\n"
                         "[NOTE: Pressed A and dialogue advanced]\n"
                         "[NOTE: Professor Birch is talking]\n"
                         "[NOTE: Now he's asking my name]\n\n"
                         "GOOD (note only when needed):\n"
                         "[NOTE: OBJECTIVE - Name character and complete intro]\n"
                         "(many turns pass with no notes)\n"
                         "[NOTE: Rival's name is MAY - might be important later]\n\n"
                         "If you don't have anything important to remember, DON'T WRITE A NOTE.\n"
                         "Most turns should have zero notes.\n\n"
                         "## RESPONSE FORMAT\n"
                         "LAST ACTION: [what you did last turn]\n\n"
                         "VERIFICATION: [Did it work? What evidence?]\n\n"
                         "CURRENT SCREEN: [what you see]\n\n"
                         "OBJECTIVE: [current goal]\n\n"
                         "PLAN: [what you'll try]\n\n"
                         "INPUTS: [your inputs]\n\n"
                         "(OPTIONAL - only if important) [NOTE: critical information to remember]\n\n"
                         "## KEY RULES\n"
                         "1. IF STUCK: Don't repeat failed inputs. Try something NEW.\n"
                         "2. BEFORE LEAVING: Interact with objects/NPCs first (press A).\n"
                         "3. READ DIALOGUE: NPCs give hints. Note them.\n"
                         "4. GROUND TRUTH: Position/map data overrides your notes if they conflict.\n"
                         "5. POKEMON EMERALD START: Bedroom -> set wall clock -> downstairs -> mom talks -> can leave.\n\n";
    
    // Add input history by turns - CRITICAL for preventing loops
    if (!m_turnHistory.isEmpty()) {
        promptText += "## Recent Input History (last 10 turns):\n";
        for (int i = 0; i < m_turnHistory.size(); ++i) {
            const auto& turn = m_turnHistory[i];
            promptText += QString("Turn %1: %2\n").arg(i + 1).arg(turn.inputs.join(", "));
        }
        promptText += "\n";
    } else {
        promptText += "## Recent Input History:\n";
        promptText += "No previous inputs recorded.\n\n";
    }

    // Add action-result history - CRITICAL for verification
    if (!m_turnRecords.isEmpty()) {
        promptText += "## ACTION HISTORY (Last 5 Turns with Results):\n";
        int recordsToShow = qMin(5, m_turnRecords.size());
        int startIdx = m_turnRecords.size() - recordsToShow;
        for (int i = startIdx; i < m_turnRecords.size(); ++i) {
            const auto& record = m_turnRecords[i];
            QString posInfo;
            if (record.hadPosition) {
                posInfo = QString("(%1,%2) -> (%3,%4)")
                    .arg(record.positionBeforeX).arg(record.positionBeforeY)
                    .arg(record.positionAfterX).arg(record.positionAfterY);
                if (!record.positionChanged) {
                    posInfo += " [NO MOVEMENT]";
                }
            } else {
                posInfo = "[position unknown]";
            }
            promptText += QString("Turn %1: %2 -> %3 (Position: %4)\n")
                .arg(record.turnNumber)
                .arg(record.inputs.join(", "))
                .arg(record.result)
                .arg(posInfo);
            if (!record.resultReason.isEmpty()) {
                promptText += QString("  Reason: %1\n").arg(record.resultReason);
            }
        }
        promptText += "\n";

        // Add interpretation help
        int failedCount = 0;
        QString lastFailedDirection;
        for (int i = qMax(0, m_turnRecords.size() - 5); i < m_turnRecords.size(); ++i) {
            if (m_turnRecords[i].result == "FAILED") {
                failedCount++;
                if (!m_turnRecords[i].inputs.isEmpty()) {
                    QString firstInput = m_turnRecords[i].inputs.first();
                    if (firstInput.contains("up") || firstInput.contains("down") ||
                        firstInput.contains("left") || firstInput.contains("right")) {
                        lastFailedDirection = firstInput;
                    }
                }
            }
        }

        if (failedCount >= 3) {
            promptText += QString("WARNING: %1 of your last 5 actions FAILED. ").arg(failedCount);
            if (!lastFailedDirection.isEmpty()) {
                promptText += QString("Movement (%1) is not working. ").arg(lastFailedDirection);
            }
            promptText += "You are likely stuck or need to do something else first (interact with object, talk to NPC, set clock).\n\n";
        }
    }

    // Add notes - CRITICAL for Claude's memory (with verification status)
    qDebug() << "=== NOTES IN PROMPT ===";
    qDebug() << "Current notes count:" << m_claudeNotes.size();
    if (!m_claudeNotes.isEmpty()) {
        promptText += "## Your Current Notes:\n";
        for (int i = 0; i < m_claudeNotes.size(); ++i) {
            const auto& note = m_claudeNotes[i];
            QString statusTag;
            if (!note.verificationStatus.isEmpty()) {
                statusTag = QString(" [%1]").arg(note.verificationStatus);
            }
            // Use actual note ID, not array index, so [CLEAR NOTE: X] works correctly
            QString noteText = QString("%1. %2%3\n").arg(note.id).arg(note.content).arg(statusTag);
            promptText += noteText;
            qDebug() << "  Note" << note.id << ":" << note.content;
        }
        promptText += "\n";
    } else {
        promptText += "## Your Current Notes:\n";
        promptText += "You have no notes. Use [NOTE: ...] to remember things.\n\n";
        qDebug() << "  (no notes)";
    }
    qDebug() << "======================";

    
    // Check for stuck detection
    QString stuckWarning = checkForStuckPattern();
    if (!stuckWarning.isEmpty()) {
        promptText += stuckWarning + "\n";
    }
    
    // Add game state if available - GROUND TRUTH
    QString gameState = readGameState();
    if (!gameState.isEmpty()) {
        promptText += "## GROUND TRUTH (This overrides your notes if they conflict)\n";
        promptText += gameState + "\n";
        promptText += "If ground truth contradicts your notes, your notes are WRONG. Update your understanding.\n\n";
    }
    
    // Add search results if available
    if (!m_pendingSearchResults.isEmpty()) {
        promptText += "Search results:\n" + m_pendingSearchResults + "\n";
        m_pendingSearchResults.clear(); // Clear after use
    }
    
    // Add search instruction if web search is enabled
    if (m_webSearchEnabled) {
        promptText += "You can search for information with [SEARCH: query here].\n\n";
    }
    
    // Final instruction
    if (!m_previousScreenshot.isEmpty()) {
        promptText += "## SCREENSHOTS\n";
        promptText += "Two images follow: PREVIOUS (before action) and CURRENT (after action).\n";
        promptText += "Compare them - if identical, your action FAILED.\n\n";
    }
    promptText += "What do you do?";

    textContent["text"] = promptText;
    content.append(textContent);

    // Add previous screenshot if available (for comparison)
    if (!m_previousScreenshot.isEmpty()) {
        QJsonObject prevImageContent;
        prevImageContent["type"] = "image";
        QJsonObject prevSource;
        prevSource["type"] = "base64";
        prevSource["media_type"] = "image/png";
        prevSource["data"] = QString(m_previousScreenshot.toBase64());
        prevImageContent["source"] = prevSource;
        content.append(prevImageContent);
    }

    // Add current screenshot
    QJsonObject imageContent;
    imageContent["type"] = "image";
    QJsonObject source;
    source["type"] = "base64";
    source["media_type"] = "image/png";
    source["data"] = QString(currentScreenshot.toBase64());
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
    while (m_conversationMessages.size() > MAX_CONVERSATION_HISTORY) {
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

    // Store current screenshot for next turn's comparison
    m_previousScreenshot = currentScreenshot;

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
    
    // Parse content - handle both thinking and text blocks
    if (response.contains("content")) {
        QJsonArray contentArray = response["content"].toArray();
        if (!contentArray.isEmpty()) {
            // When thinking is enabled, we get both "thinking" and "text" blocks
            // We need to extract only the "text" blocks for actual game inputs
            QString allTextContent;
            bool foundTextContent = false;
            
            for (const QJsonValue& contentVal : contentArray) {
                QJsonObject contentObj = contentVal.toObject();
                if (contentObj["type"].toString() == "text") {
                    QString textContent = contentObj["text"].toString();
                    if (!allTextContent.isEmpty()) {
                        allTextContent += "\n";
                    }
                    allTextContent += textContent;
                    foundTextContent = true;
                }
                // Skip "thinking" blocks - they're just internal reasoning
            }
            
            if (foundTextContent) {
                m_lastResponse = allTextContent;

                // Parse inputs FIRST so we can validate notes against them
                QList<ClaudeInput> inputs;
                parseInputsFromResponse(allTextContent, inputs);
                m_lastInputs = inputs;

                // Parse notes WITH validation against current inputs (to detect predictions)
                parseNotesFromResponse(allTextContent, inputs);
                parseSearchRequestFromResponse(allTextContent);

                // Create turn record for verification system
                if (!inputs.isEmpty()) {
                    m_turnCounter++;
                    TurnRecord record;
                    record.turnNumber = m_turnCounter;
                    record.timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
                    for (const auto& input : inputs) {
                        QString inputStr = input.button;
                        if (input.count > 1) {
                            inputStr += QString(" %1").arg(input.count);
                        }
                        record.inputs.append(inputStr);
                    }

                    // Position tracking
                    record.hadPosition = m_hasPositionBefore;
                    record.positionBeforeX = m_positionBeforeX;
                    record.positionBeforeY = m_positionBeforeY;
                    record.positionAfterX = m_lastKnownX;
                    record.positionAfterY = m_lastKnownY;

                    // Determine if position changed
                    if (m_hasPositionBefore && m_hasKnownPosition) {
                        record.positionChanged = (m_positionBeforeX != m_lastKnownX ||
                                                 m_positionBeforeY != m_lastKnownY);
                    } else {
                        record.positionChanged = false;
                    }

                    // Determine success/failure
                    // If it was a movement command and position didn't change, it failed
                    bool isMovementCommand = false;
                    for (const auto& input : inputs) {
                        if (input.button == "up" || input.button == "down" ||
                            input.button == "left" || input.button == "right") {
                            isMovementCommand = true;
                            break;
                        }
                    }

                    if (isMovementCommand) {
                        if (record.hadPosition) {
                            if (record.positionChanged) {
                                record.result = "SUCCESS";
                                record.resultReason = "Position changed";
                            } else {
                                record.result = "FAILED";
                                record.resultReason = "Position unchanged - movement blocked or action needed first";
                            }
                        } else {
                            record.result = "UNKNOWN";
                            record.resultReason = "Position data not available";
                        }
                    } else {
                        // For non-movement commands (A, B, etc.), we can't easily verify
                        // TODO: Could add dialogue detection in the future
                        record.result = "UNKNOWN";
                        record.resultReason = "Non-movement action - cannot auto-verify";
                    }

                    // Add to turn records
                    m_turnRecords.append(record);
                    while (m_turnRecords.size() > MAX_TURN_RECORDS) {
                        m_turnRecords.removeFirst();
                    }

                    qDebug() << QString("Turn %1: %2 -> %3 (Position: %4,%5 -> %6,%7)")
                        .arg(record.turnNumber)
                        .arg(record.inputs.join(", "))
                        .arg(record.result)
                        .arg(record.positionBeforeX).arg(record.positionBeforeY)
                        .arg(record.positionAfterX).arg(record.positionAfterY);
                }

                // Append assistant message to history (text only)
                QJsonObject assistantMsg;
                assistantMsg["role"] = "assistant";
                QJsonArray assistantContent;
                QJsonObject assistantText;
                assistantText["type"] = "text";
                assistantText["text"] = allTextContent;
                assistantContent.append(assistantText);
                assistantMsg["content"] = assistantContent;
                m_conversationMessages.append(assistantMsg);

                // Trim history
                while (m_conversationMessages.size() > MAX_CONVERSATION_HISTORY) {
                    m_conversationMessages.removeFirst();
                }
                saveSessionToDisk();
                
                emit responseReceived(allTextContent);
                emit inputsGenerated(inputs);
                
                // Process inputs LAST, after notes are saved
                processInputs(inputs);
            } else {
                emit errorOccurred("No text content found in response (only thinking blocks)");
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
    
    // First, try to find the INPUTS: line specifically
    QStringList lines = response.split('\n');
    QString inputLine;
    
    for (const QString& line : lines) {
        QString trimmedLine = line.trimmed().toLower();
        if (trimmedLine.startsWith("inputs:") || trimmedLine.startsWith("input:") || 
            trimmedLine.startsWith("actions:") || trimmedLine.startsWith("action:")) {
            inputLine = line.mid(line.indexOf(':') + 1).trimmed();
            break;
        }
    }
    
    // If we didn't find an INPUTS: line, fall back to searching the whole response
    if (inputLine.isEmpty()) {
        inputLine = response;
    }
    
    // Clean up the input line
    QString cleaned = inputLine.toLower();
    // Remove markdown formatting
    cleaned = cleaned.replace(QRegularExpression("[`*_]"), "");
    // Remove punctuation but keep spaces
    cleaned = cleaned.replace(QRegularExpression("[.,!?;:\"']"), " ");
    cleaned = cleaned.simplified();
    
    // Parse patterns like "up 3", "a", "start", "b 2", "a a a"
    QRegularExpression re("\\b(up|down|left|right|a|b|l|r|start|select)(?:\\s+(\\d+))?\\b");
    QRegularExpressionMatchIterator it = re.globalMatch(cleaned);
    
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
    
    // If no inputs found at all, try emergency fallback with just button names
    if (inputs.isEmpty()) {
        QStringList buttonNames = {"up", "down", "left", "right", "a", "b", "l", "r", "start", "select"};
        for (const QString& button : buttonNames) {
            if (cleaned.contains(button)) {
                ClaudeInput input;
                input.button = button;
                input.count = 1;
                input.reasoning = response;
                inputs.append(input);
                qDebug() << "Emergency fallback: found button" << button;
                break; // Only take the first match
            }
        }
    }
    
    // Ultimate fallback - if still no inputs, send 'a' to keep the game moving
    if (inputs.isEmpty()) {
        ClaudeInput input;
        input.button = "a";
        input.count = 1;
        input.reasoning = "No valid inputs found, defaulting to A button";
        inputs.append(input);
        qDebug() << "Ultimate fallback: no inputs found, sending 'a'";
    }
    
    return response;
}

void ClaudeController::parseNotesFromResponse(const QString& response, const QList<ClaudeInput>& currentInputs) {
    // Look for [CLEAR NOTE: X] commands FIRST (before adding new notes)
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

    // Build list of current action buttons for validation
    QStringList currentButtons;
    for (const auto& input : currentInputs) {
        currentButtons.append(input.button.toLower());
    }

    // Look for [NOTE: ...] commands
    QRegularExpression noteRegex("\\[NOTE:\\s*(.+?)\\]", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator noteIt = noteRegex.globalMatch(response);

    while (noteIt.hasNext()) {
        QRegularExpressionMatch match = noteIt.next();
        QString noteContent = match.captured(1).trimmed();
        if (noteContent.isEmpty()) {
            continue;
        }

        // VALIDATION: Check if note is making predictions about current action
        QString noteLower = noteContent.toLower();
        bool isPrediction = false;
        QString predictedButton;

        // Check if note mentions current action with failure indicators
        QStringList failureIndicators = {
            "didn't work", "didn't open", "didn't advance", "didn't change",
            "nothing happened", "no change", "no effect", "unchanged",
            "failed", "unsuccessful", "no response", "no result"
        };

        for (const QString& button : currentButtons) {
            // Check if note mentions this button
            if (noteLower.contains(button)) {
                // Check if it's claiming a result
                for (const QString& indicator : failureIndicators) {
                    if (noteLower.contains(indicator)) {
                        isPrediction = true;
                        predictedButton = button;
                        break;
                    }
                }
                // Also check for success claims
                if (noteLower.contains("opened") || noteLower.contains("worked") ||
                    noteLower.contains("succeeded") || noteLower.contains("changed")) {
                    isPrediction = true;
                    predictedButton = button;
                }
                if (isPrediction) break;
            }
        }

        if (isPrediction) {
            // This note is predicting the result of the current action
            QString warningNote = QString("[PREDICTION - NOT VERIFIED] %1 (Claimed result of '%2' before seeing outcome)")
                .arg(noteContent)
                .arg(predictedButton.toUpper());
            qDebug() << "WARNING: Claude wrote predictive note about current action:" << noteContent;
            qDebug() << "         This violates NOTE TIMING RULE. Marking as PREDICTION.";
            addNote(warningNote);
        } else {
            // Normal note about verified information
            addNote(noteContent);
        }
    }
}

void ClaudeController::addNote(const QString& content) {
    ClaudeNote note;
    note.id = m_nextNoteId++;
    note.timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    note.content = content;
    note.verificationStatus = "UNVERIFIED"; // New notes are unverified by default
    note.writtenThisTurn = true; // Mark as written this turn

    m_claudeNotes.append(note);

    qDebug() << "=== NOTE ADDED ===";
    qDebug() << "Note ID:" << note.id;
    qDebug() << "Content:" << content;
    qDebug() << "Total notes:" << m_claudeNotes.size();

    // Keep only last MAX_NOTES notes (FIFO)
    while (m_claudeNotes.size() > MAX_NOTES) {
        m_claudeNotes.removeFirst();
        qDebug() << "Removed oldest note (exceeded MAX_NOTES)";
    }

    // Renumber notes to keep IDs sequential and manageable
    // This prevents note IDs from growing excessively large
    if (m_nextNoteId > MAX_NOTES * 2) { // If IDs are getting large, renumber
        qDebug() << "Renumbering notes (IDs growing large)";
        for (int i = 0; i < m_claudeNotes.size(); ++i) {
            m_claudeNotes[i].id = i + 1;
        }
        m_nextNoteId = m_claudeNotes.size() + 1;
    }
    qDebug() << "==================";

    emit notesChanged();
    saveSessionToDisk(); // Persist notes
}

void ClaudeController::clearNote(int noteId) {
    qDebug() << "=== CLEAR NOTE ===";
    qDebug() << "Attempting to clear note ID:" << noteId;
    bool found = false;
    for (int i = 0; i < m_claudeNotes.size(); ++i) {
        if (m_claudeNotes[i].id == noteId) {
            qDebug() << "Found and removing note:" << m_claudeNotes[i].content;
            m_claudeNotes.removeAt(i);
            qDebug() << "Remaining notes:" << m_claudeNotes.size();
            emit notesChanged();
            saveSessionToDisk();
            found = true;
            break;
        }
    }
    if (!found) {
        qDebug() << "Note ID" << noteId << "not found";
    }
    qDebug() << "==================";
}

void ClaudeController::clearAllNotes() {
    qDebug() << "=== CLEAR ALL NOTES ===";
    if (!m_claudeNotes.isEmpty()) {
        int count = m_claudeNotes.size();
        qDebug() << "Clearing" << count << "notes";
        m_claudeNotes.clear();
        // Reset note ID counter so numbers start from 1 again
        m_nextNoteId = 1;
        emit notesChanged();
        saveSessionToDisk();
        qDebug() << "All notes cleared successfully";
    } else {
        qDebug() << "No notes to clear";
    }
    qDebug() << "=======================";
}

void ClaudeController::validateNotesAgainstGroundTruth() {
    // Clear writtenThisTurn flag for all notes from previous turn
    for (auto& note : m_claudeNotes) {
        note.writtenThisTurn = false;
    }

    // If we don't have position data, we can't validate location-based notes
    if (!m_hasKnownPosition) {
        return;
    }

    // Check notes for contradictions with ground truth
    // Look for location claims that contradict current position
    QStringList locationKeywords = {
        "downstairs", "down stairs", "first floor", "1f",
        "upstairs", "up stairs", "second floor", "2f", "bedroom",
        "outside", "left the house", "left house", "exited",
        "route 101", "littleroot", "town"
    };

    // Simple heuristic: if we're at the same position for multiple turns
    // and notes claim we moved, mark them as contradicted
    bool hasContradiction = false;
    for (auto& note : m_claudeNotes) {
        QString noteLC = note.content.toLower();

        // Check for movement claims when position hasn't changed
        if ((noteLC.contains("moved") || noteLC.contains("went") ||
             noteLC.contains("left") || noteLC.contains("reached") ||
             noteLC.contains("made it")) &&
            !note.writtenThisTurn) {

            // If the note was written in a previous turn and we haven't moved
            // significantly from where we were when it was written, it might be wrong
            // This is a simple heuristic - could be improved with more context

            // For now, just mark location-specific claims as potentially contradicted
            for (const QString& keyword : locationKeywords) {
                if (noteLC.contains(keyword)) {
                    note.verificationStatus = "CONTRADICTED";
                    hasContradiction = true;
                    break;
                }
            }
        }
    }

    if (hasContradiction) {
        emit notesChanged();
        saveSessionToDisk();
    }
}

QString ClaudeController::checkForStuckPattern() const {
    if (m_turnHistory.size() < 2) {
        return QString(); // Need at least 2 turns to detect patterns
    }
    
    // Check for repeated directional inputs across recent turns
    QString repeatedDirection;
    int directionCount = 0;
    bool hasRecentMovement = false;
    
    // Look at last 3 turns for patterns
    int turnsToCheck = qMin(3, m_turnHistory.size());
    for (int i = m_turnHistory.size() - turnsToCheck; i < m_turnHistory.size(); ++i) {
        const auto& turn = m_turnHistory[i];
        
        for (const QString& input : turn.inputs) {
            QString button = input.split(" ").first().toLower();
            
            if (isDirectionalButton(button)) {
                hasRecentMovement = true;
                if (repeatedDirection.isEmpty()) {
                    repeatedDirection = button;
                    directionCount = 1;
                } else if (button == repeatedDirection) {
                    directionCount++;
                }
            }
        }
    }
    
    // Trigger stuck detection if same direction used 4+ times across recent turns
    if (directionCount >= 4 && hasRecentMovement) {
        QString advice = QString("## STUCK WARNING\n"
                               "You've pressed %1 repeatedly without progress. This usually means:\n"
                               "1. There's an obstacle or NPC blocking you\n"
                               "2. You need to complete an action first (interact with something, set clock, talk to someone)\n"
                               "3. You're in a menu and need to press B to exit\n\n"
                               "Try: Press A to interact with whatever is in front of you, or look for objects in the room you haven't examined.")
                              .arg(repeatedDirection.toUpper());
        
        return advice;
    }
    
    return QString();
}

QString ClaudeController::readGameState() {
    QFile file(GAME_STATE_PATH);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString(); // No game state file
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    if (data.isEmpty()) {
        return QString();
    }
    
    // Parse the JSON to extract useful information
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString(); // Invalid JSON
    }
    
    QJsonObject gameState = doc.object();
    
    if (gameState.contains("error")) {
        return QString(); // Error in game state (game not loaded, etc.)
    }
    
    if (gameState.contains("x") && gameState.contains("y") && gameState.contains("in_battle")) {
        int x = gameState["x"].toInt();
        int y = gameState["y"].toInt();
        bool inBattle = gameState["in_battle"].toBool();

        QString result = QString("Position: (%1, %2)").arg(x).arg(y);

        // Add map info if available
        if (gameState.contains("map_group") && gameState.contains("map_num")) {
            int mapGroup = gameState["map_group"].toInt();
            int mapNum = gameState["map_num"].toInt();
            result += QString("\nMap: Group %1, Num %2").arg(mapGroup).arg(mapNum);

            // Add human-readable map names for common locations
            // Map Group 0 = Littleroot Town area
            if (mapGroup == 0) {
                if (mapNum == 0) result += " (Petalburg City)";
                else if (mapNum == 1) result += " (Slateport City)";
                else if (mapNum == 2) result += " (Mauville City)";
                else if (mapNum == 3) result += " (Rustboro City)";
                else if (mapNum == 4) result += " (Fortree City)";
                else if (mapNum == 5) result += " (Lilycove City)";
                else if (mapNum == 6) result += " (Mossdeep City)";
                else if (mapNum == 7) result += " (Sootopolis City)";
                else if (mapNum == 8) result += " (Ever Grande City)";
                else if (mapNum == 9) result += " (Littleroot Town)";
                else if (mapNum == 10) result += " (Oldale Town)";
            }
            // Map Group 1 = Buildings/indoors
            else if (mapGroup == 1) {
                if (mapNum == 0) result += " (Player's House 1F)";
                else if (mapNum == 1) result += " (Player's House 2F - Bedroom)";
                else if (mapNum == 2) result += " (Rival's House 1F)";
                else if (mapNum == 3) result += " (Rival's House 2F)";
                else if (mapNum == 4) result += " (Prof Birch's Lab)";
            }
        }

        result += QString("\nIn battle: %1").arg(inBattle ? "yes" : "no");

        // Check if position changed since last turn (for stuck detection)
        if (m_hasKnownPosition) {
            bool positionChanged = (x != m_lastKnownX || y != m_lastKnownY);
            result += QString("\nPosition changed since last turn: %1").arg(positionChanged ? "Yes" : "No");
            if (!positionChanged && (m_lastKnownX != -1 && m_lastKnownY != -1)) {
                result += " (may be stuck!)";
            }
        }

        // Update position tracking
        m_lastKnownX = x;
        m_lastKnownY = y;
        m_hasKnownPosition = true;

        return result;
    }
    
    return QString();
}

void ClaudeController::parseSearchRequestFromResponse(const QString& response) {
    // Look for [SEARCH: query] commands
    QRegularExpression searchRegex("\\[SEARCH:\\s*(.+?)\\]", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator searchIt = searchRegex.globalMatch(response);
    
    while (searchIt.hasNext()) {
        QRegularExpressionMatch match = searchIt.next();
        QString query = match.captured(1).trimmed();
        if (!query.isEmpty() && m_webSearchEnabled) {
            performWebSearch(query);
            break; // Only handle first search request to avoid spam
        }
    }
}

void ClaudeController::performWebSearch(const QString& query) {
    // Make a separate API call with web search enabled
    QJsonObject searchRequestBody;
    searchRequestBody["model"] = modelAlias();
    searchRequestBody["max_tokens"] = 1024;
    
    QJsonArray tools;
    QJsonObject webSearchTool;
    webSearchTool["type"] = "web_search_20250305";
    webSearchTool["name"] = "web_search";
    webSearchTool["max_uses"] = 3;
    tools.append(webSearchTool);
    searchRequestBody["tools"] = tools;
    
    QJsonArray messages;
    QJsonObject message;
    message["role"] = "user";
    QJsonArray content;
    QJsonObject textContent;
    textContent["type"] = "text";
    textContent["text"] = QString("Search for: %1").arg(query);
    content.append(textContent);
    message["content"] = content;
    messages.append(message);
    
    searchRequestBody["messages"] = messages;
    
    QJsonDocument doc(searchRequestBody);
    QByteArray data = doc.toJson();
    
    qDebug() << "Making web search request for query:" << query;
    
    QNetworkRequest netRequest{QUrl(CLAUDE_API_URL)};
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    netRequest.setRawHeader("x-api-key", m_apiKey.toUtf8());
    netRequest.setRawHeader("anthropic-version", "2023-06-01");
    netRequest.setRawHeader("User-Agent", "Claudemon/1.0 (mGBA fork)");
    
    QNetworkReply* reply = m_networkManager->post(netRequest, data);
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        reply->deleteLater();
        
        QByteArray responseData = reply->readAll();
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        
        if (reply->error() == QNetworkReply::NoError && httpStatus == 200) {
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
            
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject response = doc.object();
                if (response.contains("content") && response["content"].isArray()) {
                    QJsonArray contentArray = response["content"].toArray();
                    for (const QJsonValue& contentVal : contentArray) {
                        QJsonObject contentObj = contentVal.toObject();
                        if (contentObj["type"].toString() == "text") {
                            QString searchResults = contentObj["text"].toString();
                            m_pendingSearchResults = searchResults;
                            qDebug() << "Web search completed successfully";
                            return;
                        }
                    }
                }
            }
        }
        
        qDebug() << "Web search failed:" << reply->errorString();
        m_pendingSearchResults = "Search failed: " + reply->errorString();
    });
}

void ClaudeController::processInputs(const QList<ClaudeInput>& inputs) {
    if (!m_inputController || !m_running) return;
    
    // Clear any pending inputs from previous call
    m_pendingInputs.clear();
    
    // Record inputs to history
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QStringList turnInputs;
    
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
            
            // Add to individual input history (for backwards compatibility)
            InputHistoryEntry entry;
            entry.timestamp = timestamp;
            entry.input = input.count > 1 ? QString("%1 x%2").arg(input.button).arg(input.count) : input.button;
            m_recentInputs.append(entry);
            
            // Add to turn history
            QString turnInput = input.count > 1 ? QString("%1 x%2").arg(input.button).arg(input.count) : input.button;
            turnInputs.append(turnInput);
        } else {
            qDebug() << "Unknown button:" << input.button;
        }
    }
    
    // Record this turn if we had any valid inputs
    if (!turnInputs.isEmpty()) {
        TurnHistory turn;
        turn.timestamp = timestamp;
        turn.inputs = turnInputs;
        m_turnHistory.append(turn);
        
        // Keep only recent turns
        while (m_turnHistory.size() > MAX_TURN_HISTORY) {
            m_turnHistory.removeFirst();
        }
    }

    // Keep only recent individual inputs
    while (m_recentInputs.size() > MAX_RECENT_INPUTS) {
        m_recentInputs.removeFirst();
    }
    
    // Start processing the queue
    if (!m_inputPacingTimer->isActive()) {
        processNextPendingInput();
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
        processNextPendingInput();
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

void ClaudeController::processNextPendingInput() {
    if (m_pendingInputs.isEmpty() || !m_coreController || !m_running) {
        return;
    }

    auto& front = m_pendingInputs.front();
    m_coreController->addKey(front.keyCode);
    m_currentKey = front.keyCode;
    m_currentKeyIsDirectional = front.isDirectional;

    // Save values before potentially removing from list
    bool isDirectional = front.isDirectional;
    int originalCount = front.originalCount;

    front.remainingCount--;
    if (front.remainingCount <= 0) {
        m_pendingInputs.removeFirst();
    }

    // Set timer interval based on input type (use saved values)
    if (isDirectional && originalCount > 1) {
        m_inputPacingTimer->setInterval(DIRECTION_HOLD_MS * originalCount);
    } else {
        m_inputPacingTimer->setInterval(INPUT_PACING_MS);
    }

    m_inputPacingTimer->start();
}

void ClaudeController::saveSessionToDisk() {
    QJsonObject root;
    root["model"] = modelAlias();
    root["apiKey"] = m_apiKey;
    root["thinking"] = m_thinkingEnabled;
    root["webSearch"] = m_webSearchEnabled;
    root["history"] = m_conversationMessages;
    root["nextNoteId"] = m_nextNoteId;

    // Save notes
    QJsonArray notesArray;
    for (const ClaudeNote& note : m_claudeNotes) {
        QJsonObject noteObj;
        noteObj["id"] = note.id;
        noteObj["timestamp"] = note.timestamp;
        noteObj["content"] = note.content;
        noteObj["verificationStatus"] = note.verificationStatus;
        // Don't save writtenThisTurn - it resets each turn anyway
        notesArray.append(noteObj);
    }
    root["notes"] = notesArray;

    qDebug() << "=== SAVING SESSION ===";
    qDebug() << "Saving" << m_claudeNotes.size() << "notes to disk";

    QJsonDocument doc(root);
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    QDir().mkpath(basePath);
    QString sessionPath = basePath + "/claude_session.json";
    QFile f(sessionPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qint64 bytesWritten = f.write(doc.toJson());
        f.close();
        qDebug() << "Session saved to:" << sessionPath;
        qDebug() << "Bytes written:" << bytesWritten;
    } else {
        qDebug() << "ERROR: Failed to open session file for writing:" << sessionPath;
    }
    qDebug() << "======================";
}

void ClaudeController::loadSessionFromDisk() {
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    QString sessionPath = basePath + "/claude_session.json";
    QFile f(sessionPath);

    qDebug() << "=== LOADING SESSION ===";
    qDebug() << "Session path:" << sessionPath;

    if (!f.exists()) {
        qDebug() << "Session file does not exist (first run?)";
        qDebug() << "=======================";
        return;
    }

    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "ERROR: Failed to open session file for reading";
        qDebug() << "=======================";
        return;
    }

    QByteArray data = f.readAll();
    f.close();
    qDebug() << "Read" << data.size() << "bytes from session file";

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "ERROR: Failed to parse session JSON:" << err.errorString();
        qDebug() << "=======================";
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
    m_webSearchEnabled = root["webSearch"].toBool(false);
    if (root.contains("history") && root["history"].isArray()) {
        m_conversationMessages = root["history"].toArray();
        while (m_conversationMessages.size() > MAX_CONVERSATION_HISTORY) {
            m_conversationMessages.removeFirst();
        }
    }

    // Load notes
    m_nextNoteId = root["nextNoteId"].toInt(1);
    if (root.contains("notes") && root["notes"].isArray()) {
        m_claudeNotes.clear();
        QJsonArray notesArray = root["notes"].toArray();
        qDebug() << "Loading" << notesArray.size() << "notes from session";
        for (const QJsonValue& noteVal : notesArray) {
            if (noteVal.isObject()) {
                QJsonObject noteObj = noteVal.toObject();
                ClaudeNote note;
                note.id = noteObj["id"].toInt();
                note.timestamp = noteObj["timestamp"].toString();
                note.content = noteObj["content"].toString();
                note.verificationStatus = noteObj["verificationStatus"].toString();
                note.writtenThisTurn = false; // All loaded notes are from previous session
                m_claudeNotes.append(note);
                qDebug() << "  Loaded note" << note.id << ":" << note.content;
            }
        }
        qDebug() << "Total notes loaded:" << m_claudeNotes.size();
    } else {
        qDebug() << "No notes found in session file";
    }
    qDebug() << "=======================";
}

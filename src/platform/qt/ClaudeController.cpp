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

#include <mgba/core/input.h>
#include <mgba/gba/input.h>
#include <mgba/core/core.h>
#include <mgba/core/thread.h>

using namespace QGBA;

const QString ClaudeController::CLAUDE_API_URL = "https://api.anthropic.com/v1/messages";

ClaudeController::ClaudeController(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_gameLoopTimer(new QTimer(this))
    , m_coreController(nullptr)
    , m_inputController(nullptr)
    , m_running(false)
{
    m_gameLoopTimer->setSingleShot(false);
    m_gameLoopTimer->setInterval(LOOP_INTERVAL_MS);
    
    connect(m_gameLoopTimer, &QTimer::timeout, this, &ClaudeController::captureAndSendScreenshot);
}

ClaudeController::~ClaudeController() {
    stopGameLoop();
}

void ClaudeController::setApiKey(const QString& key) {
    m_apiKey = key;
}

void ClaudeController::setCoreController(CoreController* controller) {
    m_coreController = controller;
}

void ClaudeController::setInputController(InputController* controller) {
    m_inputController = controller;
}

void ClaudeController::startGameLoop() {
    if (m_apiKey.isEmpty() || !m_coreController || !m_inputController) {
        emit errorOccurred("Missing required components (API key, core, or input controller)");
        return;
    }
    
    m_running = true;
    m_gameLoopTimer->start();
    qDebug() << "Claude game loop started";
}

void ClaudeController::stopGameLoop() {
    m_running = false;
    m_gameLoopTimer->stop();
    qDebug() << "Claude game loop stopped";
}

QByteArray ClaudeController::captureScreenshotData() {
    if (!m_coreController) {
        return QByteArray();
    }
    
    QByteArray imageData;
    QBuffer buffer(&imageData);
    buffer.open(QIODevice::WriteOnly);
    
    // Capture screenshot data from the core
    m_coreController->threadInterrupt();
    mCore* core = m_coreController->thread()->core;
    
    if (core) {
        size_t stride;
        const void* pixels = nullptr;
        unsigned width, height;
        
        core->currentVideoSize(core, &width, &height);
        core->getPixels(core, &pixels, &stride);
        
        if (pixels && width > 0 && height > 0) {
            // Convert raw pixels to QImage
            // Assuming RGB565 format (common for GBA)
            QImage image(static_cast<const uchar*>(pixels), width, height, stride, QImage::Format_RGB16);
            
            // Convert to RGB888 for better Claude processing
            QImage rgb888 = image.convertToFormat(QImage::Format_RGB888);
            
            // Save as PNG for API
            rgb888.save(&buffer, "PNG");
            
            qDebug() << "Captured screenshot:" << width << "x" << height << "pixels, size:" << imageData.size() << "bytes";
        } else {
            qDebug() << "Failed to get pixel data from core";
        }
    }
    
    m_coreController->threadContinue();
    return imageData;
}

void ClaudeController::captureAndSendScreenshot() {
    if (!m_coreController || !m_running) {
        return;
    }
    
    // Capture actual screenshot data
    QByteArray imageData = captureScreenshotData();
    
    QJsonObject requestBody;
    requestBody["model"] = "claude-3-5-sonnet-20241022";
    requestBody["max_tokens"] = 300;
    
    QJsonArray messages;
    QJsonObject message;
    message["role"] = "user";
    
    QJsonArray content;
    QJsonObject textContent;
    textContent["type"] = "text";
    textContent["text"] = "You are playing Pokemon Emerald on a Game Boy Advance emulator. "
                          "Look at this screenshot and decide what button to press next. "
                          "Your goal is to make progress in the game. "
                          "Respond with a simple button command like 'a', 'start', 'up 3', 'b 2', etc. "
                          "Only respond with the button command, nothing else.";
    content.append(textContent);
    
    // Add actual image data if we have it
    if (!imageData.isEmpty()) {
        QJsonObject imageContent;
        imageContent["type"] = "image";
        
        QJsonObject source;
        source["type"] = "base64";
        source["media_type"] = "image/png";
        source["data"] = QString(imageData.toBase64());
        
        imageContent["source"] = source;
        content.append(imageContent);
    }
    
    message["content"] = content;
    messages.append(message);
    requestBody["messages"] = messages;
    
    QJsonDocument doc(requestBody);
    QByteArray data = doc.toJson();
    
    QNetworkRequest request(QUrl(CLAUDE_API_URL));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    
    QNetworkReply* reply = m_networkManager->post(request, data);
    connect(reply, &QNetworkReply::finished, this, &ClaudeController::handleApiResponse);
    
    emit loopTick();
}

void ClaudeController::handleApiResponse() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    reply->deleteLater();
    
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(QString("Network error: %1").arg(reply->errorString()));
        return;
    }
    
    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    QJsonObject response = doc.object();
    
    if (response.contains("error")) {
        QJsonObject error = response["error"].toObject();
        emit errorOccurred(QString("API error: %1").arg(error["message"].toString()));
        return;
    }
    
    if (response.contains("content")) {
        QJsonArray contentArray = response["content"].toArray();
        if (!contentArray.isEmpty()) {
            QString responseText = contentArray[0].toObject()["text"].toString();
            m_lastResponse = responseText;
            
            QList<ClaudeInput> inputs;
            QString reasoning = parseInputsFromResponse(responseText, inputs);
            m_lastInputs = inputs;
            
            emit responseReceived(responseText);
            emit inputsGenerated(inputs);
            
            processInputs(inputs);
        }
    }
}

QString ClaudeController::parseInputsFromResponse(const QString& response, QList<ClaudeInput>& inputs) {
    inputs.clear();
    
    // Simple parsing - look for button commands
    QString trimmed = response.trimmed().toLower();
    
    // Parse patterns like "up 3", "a", "start", "b 2"
    QRegularExpression re("\\b(up|down|left|right|a|b|l|r|start|select)(?:\\s+(\\d+))?\\b");
    QRegularExpressionMatchIterator it = re.globalMatch(trimmed);
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        ClaudeInput input;
        input.button = match.captured(1);
        input.count = match.captured(2).isEmpty() ? 1 : match.captured(2).toInt();
        input.reasoning = response; // Store full response as reasoning
        inputs.append(input);
    }
    
    // If no specific pattern found, try to extract just the button name
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

void ClaudeController::processInputs(const QList<ClaudeInput>& inputs) {
    if (!m_inputController || !m_running) return;
    
    for (const ClaudeInput& input : inputs) {
        sendInputToGame(input.button, input.count);
    }
}

void ClaudeController::sendInputToGame(const QString& button, int count) {
    if (!m_inputController) return;
    
    int keyCode = getGBAKeyCode(button);
    if (keyCode < 0) {
        qDebug() << "Unknown button:" << button;
        return;
    }
    
    qDebug() << "Sending input:" << button << "x" << count;
    
    // Send the button press multiple times
    for (int i = 0; i < count; i++) {
        m_inputController->postPendingEvent(keyCode);
        // TODO: Add small delay between presses if needed
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
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

namespace QGBA {

class CoreController;
class InputController;

struct ClaudeInput {
    QString button;
    int count = 1;
    QString reasoning;
};

class ClaudeController : public QObject {
Q_OBJECT

public:
    ClaudeController(QObject* parent = nullptr);
    ~ClaudeController();

    void setApiKey(const QString& key);
    void setCoreController(CoreController* controller);
    void setInputController(InputController* controller);
    
    void startGameLoop();
    void stopGameLoop();
    bool isRunning() const { return m_running; }
    
    QString getLastResponse() const { return m_lastResponse; }
    QList<ClaudeInput> getLastInputs() const { return m_lastInputs; }

signals:
    void responseReceived(const QString& response);
    void inputsGenerated(const QList<ClaudeInput>& inputs);
    void errorOccurred(const QString& error);
    void loopTick();

private slots:
    void captureAndSendScreenshot();
    void handleApiResponse();
    void processInputs(const QList<ClaudeInput>& inputs);

private:
    QString parseInputsFromResponse(const QString& response, QList<ClaudeInput>& inputs);
    void sendInputToGame(const QString& button, int count);
    int getGBAKeyCode(const QString& button);
    
    QNetworkAccessManager* m_networkManager;
    QTimer* m_gameLoopTimer;
    
    CoreController* m_coreController;
    InputController* m_inputController;
    
    QString m_apiKey;
    bool m_running;
    
    QString m_lastResponse;
    QList<ClaudeInput> m_lastInputs;
    
    static const QString CLAUDE_API_URL;
    static const int LOOP_INTERVAL_MS = 2000; // 2 seconds between actions
};

}
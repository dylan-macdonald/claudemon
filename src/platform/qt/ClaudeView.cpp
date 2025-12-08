/* Copyright (c) 2024 Claudemon Project
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClaudeView.h"
#include <QGridLayout>
#include <QSplitter>
#include <QDateTime>
#include <QScrollBar>

using namespace QGBA;

ClaudeView::ClaudeView(QWidget* parent)
    : QWidget(parent)
    , m_claudeController(nullptr)
    , m_statusTimer(new QTimer(this))
    , m_loopCount(0)
{
    setupUI();
    
    m_statusTimer->setSingleShot(false);
    m_statusTimer->setInterval(1000); // Update every second
    connect(m_statusTimer, &QTimer::timeout, this, &ClaudeView::updateStatus);
    m_statusTimer->start();
}

void ClaudeView::setupUI() {
    m_mainLayout = new QVBoxLayout(this);
    
    // API Key and Control Section
    m_apiGroup = new QGroupBox("Claude API Configuration", this);
    QVBoxLayout* apiLayout = new QVBoxLayout(m_apiGroup);
    
    QHBoxLayout* keyLayout = new QHBoxLayout;
    keyLayout->addWidget(new QLabel("API Key:", this));
    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText("Enter your Claude API key...");
    keyLayout->addWidget(m_apiKeyEdit);
    
    QHBoxLayout* controlLayout = new QHBoxLayout;
    m_startStopButton = new QPushButton("Start Claude", this);
    m_statusLabel = new QLabel("Stopped", this);
    m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
    controlLayout->addWidget(m_startStopButton);
    controlLayout->addWidget(m_statusLabel);
    controlLayout->addStretch();
    
    apiLayout->addLayout(keyLayout);
    apiLayout->addLayout(controlLayout);
    m_mainLayout->addWidget(m_apiGroup);
    
    // Create splitter for the main content areas
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    
    // Claude Response Section (Left side)
    m_responseGroup = new QGroupBox("Claude's Reasoning", this);
    QVBoxLayout* responseLayout = new QVBoxLayout(m_responseGroup);
    m_responseText = new QTextEdit(this);
    m_responseText->setReadOnly(true);
    m_responseText->setPlaceholderText("Claude's responses and reasoning will appear here...");
    responseLayout->addWidget(m_responseText);
    splitter->addWidget(m_responseGroup);
    
    // Right side container
    QWidget* rightContainer = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightContainer);
    
    // Input History Section (Right top)
    m_inputGroup = new QGroupBox("Input History", this);
    QVBoxLayout* inputLayout = new QVBoxLayout(m_inputGroup);
    m_inputList = new QListWidget(this);
    inputLayout->addWidget(m_inputList);
    rightLayout->addWidget(m_inputGroup);
    
    // Status Section (Right bottom)
    m_statusGroup = new QGroupBox("Status", this);
    QGridLayout* statusLayout = new QGridLayout(m_statusGroup);
    
    statusLayout->addWidget(new QLabel("Loop Count:", this), 0, 0);
    m_loopCountLabel = new QLabel("0", this);
    statusLayout->addWidget(m_loopCountLabel, 0, 1);
    
    statusLayout->addWidget(new QLabel("Last Action:", this), 1, 0);
    m_lastActionLabel = new QLabel("None", this);
    statusLayout->addWidget(m_lastActionLabel, 1, 1);
    
    rightLayout->addWidget(m_statusGroup);
    
    splitter->addWidget(rightContainer);
    splitter->setStretchFactor(0, 2); // Response gets more space
    splitter->setStretchFactor(1, 1);
    
    m_mainLayout->addWidget(splitter);
    
    // Connect signals
    connect(m_startStopButton, &QPushButton::clicked, this, &ClaudeView::onStartStopClicked);
    connect(m_apiKeyEdit, &QLineEdit::textChanged, this, &ClaudeView::onApiKeyChanged);
    
    updateButtonStates();
}

void ClaudeView::setClaudeController(ClaudeController* controller) {
    if (m_claudeController) {
        // Disconnect old controller
        disconnect(m_claudeController, nullptr, this, nullptr);
    }
    
    m_claudeController = controller;
    
    if (m_claudeController) {
        // Connect new controller
        connect(m_claudeController, &ClaudeController::responseReceived, 
                this, &ClaudeView::onClaudeResponseReceived);
        connect(m_claudeController, &ClaudeController::inputsGenerated, 
                this, &ClaudeView::onClaudeInputsGenerated);
        connect(m_claudeController, &ClaudeController::errorOccurred, 
                this, &ClaudeView::onClaudeErrorOccurred);
        connect(m_claudeController, &ClaudeController::loopTick, 
                this, &ClaudeView::onLoopTick);
    }
    
    updateButtonStates();
}

void ClaudeView::onStartStopClicked() {
    if (!m_claudeController) return;
    
    if (m_claudeController->isRunning()) {
        m_claudeController->stopGameLoop();
        m_startStopButton->setText("Start Claude");
        m_statusLabel->setText("Stopped");
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
    } else {
        QString apiKey = m_apiKeyEdit->text().trimmed();
        if (apiKey.isEmpty()) {
            m_statusLabel->setText("Error: API key required");
            m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
            return;
        }
        
        m_claudeController->setApiKey(apiKey);
        m_claudeController->startGameLoop();
        m_startStopButton->setText("Stop Claude");
        m_statusLabel->setText("Running");
        m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
        m_loopCount = 0;
    }
}

void ClaudeView::onApiKeyChanged() {
    updateButtonStates();
}

void ClaudeView::onClaudeResponseReceived(const QString& response) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString formattedResponse = QString("[%1] %2").arg(timestamp, response);
    
    m_responseText->append(formattedResponse);
    m_responseText->append(""); // Add empty line for readability
    
    // Auto-scroll to bottom
    QScrollBar* scrollBar = m_responseText->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void ClaudeView::onClaudeInputsGenerated(const QList<ClaudeInput>& inputs) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    
    for (const ClaudeInput& input : inputs) {
        QString inputText = QString("[%1] %2").arg(timestamp, input.button);
        if (input.count > 1) {
            inputText += QString(" x%1").arg(input.count);
        }
        
        m_inputList->addItem(inputText);
        m_lastActionLabel->setText(QString("%1 x%2").arg(input.button).arg(input.count));
    }
    
    // Auto-scroll to bottom
    m_inputList->scrollToBottom();
    
    // Keep only last 100 items to prevent memory issues
    while (m_inputList->count() > 100) {
        delete m_inputList->takeItem(0);
    }
}

void ClaudeView::onClaudeErrorOccurred(const QString& error) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString errorMsg = QString("[%1] ERROR: %2").arg(timestamp, error);
    
    m_responseText->append(errorMsg);
    m_responseText->append("");
    
    m_statusLabel->setText("Error occurred");
    m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
    
    // Auto-scroll to bottom
    QScrollBar* scrollBar = m_responseText->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void ClaudeView::onLoopTick() {
    m_loopCount++;
    m_loopCountLabel->setText(QString::number(m_loopCount));
}

void ClaudeView::updateStatus() {
    // Additional periodic status updates can go here
    updateButtonStates();
}

void ClaudeView::updateButtonStates() {
    bool hasApiKey = !m_apiKeyEdit->text().trimmed().isEmpty();
    bool hasController = m_claudeController != nullptr;
    
    m_startStopButton->setEnabled(hasApiKey && hasController);
    
    if (hasController && m_claudeController->isRunning()) {
        m_startStopButton->setText("Stop Claude");
    } else {
        m_startStopButton->setText("Start Claude");
    }
}
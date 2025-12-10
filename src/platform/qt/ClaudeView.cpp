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

    QHBoxLayout* modelLayout = new QHBoxLayout;
    modelLayout->addWidget(new QLabel("Model:", this));
    m_modelCombo = new QComboBox(this);
    m_modelCombo->addItem("Opus 4.5", QVariant::fromValue(static_cast<int>(ClaudeController::ModelOpus)));
    m_modelCombo->addItem("Sonnet 4.5", QVariant::fromValue(static_cast<int>(ClaudeController::ModelSonnet)));
    m_modelCombo->addItem("Haiku 4.5", QVariant::fromValue(static_cast<int>(ClaudeController::ModelHaiku)));
    // Default to Sonnet
    int sonnetIdx = m_modelCombo->findData(QVariant::fromValue(static_cast<int>(ClaudeController::ModelSonnet)));
    if (sonnetIdx >= 0) {
        m_modelCombo->setCurrentIndex(sonnetIdx);
    }
    modelLayout->addWidget(m_modelCombo);

    m_thinkingCheck = new QCheckBox("Thinking", this);
    modelLayout->addWidget(m_thinkingCheck);
    modelLayout->addStretch();
    
    QHBoxLayout* controlLayout = new QHBoxLayout;
    m_startStopButton = new QPushButton("Start Claude", this);
    m_statusLabel = new QLabel("Stopped", this);
    m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
    controlLayout->addWidget(m_startStopButton);
    controlLayout->addWidget(m_statusLabel);
    controlLayout->addStretch();
    
    apiLayout->addLayout(keyLayout);
    apiLayout->addLayout(modelLayout);
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
    
    // Notes Section (Right middle)
    m_notesGroup = new QGroupBox("Claude's Notes", this);
    QVBoxLayout* notesLayout = new QVBoxLayout(m_notesGroup);
    m_notesList = new QListWidget(this);
    m_notesList->setMaximumHeight(150); // Keep it compact
    notesLayout->addWidget(m_notesList);
    rightLayout->addWidget(m_notesGroup);
    
    // Status Section (Right bottom)
    m_statusGroup = new QGroupBox("Status", this);
    QGridLayout* statusLayout = new QGridLayout(m_statusGroup);
    
    statusLayout->addWidget(new QLabel("Loop Count:", this), 0, 0);
    m_loopCountLabel = new QLabel("0", this);
    statusLayout->addWidget(m_loopCountLabel, 0, 1);
    
    statusLayout->addWidget(new QLabel("Last Action:", this), 1, 0);
    m_lastActionLabel = new QLabel("None", this);
    statusLayout->addWidget(m_lastActionLabel, 1, 1);
    
    statusLayout->addWidget(new QLabel("Errors:", this), 2, 0);
    m_errorCountLabel = new QLabel("0", this);
    m_errorCountLabel->setStyleSheet("color: gray;");
    statusLayout->addWidget(m_errorCountLabel, 2, 1);
    
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
        connect(m_claudeController, &ClaudeController::notesChanged, 
                this, &ClaudeView::onClaudeNotesChanged);
        connect(m_claudeController, &ClaudeController::errorOccurred, 
                this, &ClaudeView::onClaudeErrorOccurred);
        connect(m_claudeController, &ClaudeController::criticalError, 
                this, &ClaudeView::onClaudeCriticalError);
        connect(m_claudeController, &ClaudeController::loopTick, 
                this, &ClaudeView::onLoopTick);
        connect(m_claudeController, &ClaudeController::gameReadyChanged,
                this, &ClaudeView::updateButtonStates);

        // Sync UI with persisted session (block signals to avoid triggering saves)
        m_apiKeyEdit->blockSignals(true);
        m_apiKeyEdit->setText(m_claudeController->apiKey());
        m_apiKeyEdit->blockSignals(false);
        
        ClaudeController::Model model = m_claudeController->model();
        int idx = m_modelCombo->findData(QVariant::fromValue(static_cast<int>(model)));
        if (idx >= 0) m_modelCombo->setCurrentIndex(idx);
        m_thinkingCheck->setChecked(m_claudeController->thinkingEnabled());
        
        // Update notes display
        onClaudeNotesChanged();
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

        // Configure model and thinking
        QVariant data = m_modelCombo->currentData();
        ClaudeController::Model model = ClaudeController::ModelSonnet;
        if (data.isValid()) {
            model = static_cast<ClaudeController::Model>(data.toInt());
        }
        m_claudeController->setModel(model);
    m_claudeController->setThinkingEnabled(m_thinkingCheck->isChecked());
        
        m_claudeController->setApiKey(apiKey);
        m_claudeController->startGameLoop();
        
        // Check if it actually started (might fail if no ROM loaded)
        if (m_claudeController->isRunning()) {
            m_startStopButton->setText("Stop Claude");
            m_statusLabel->setText("Running");
            m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
            m_loopCount = 0;
            m_errorCountLabel->setText("0");
            m_errorCountLabel->setStyleSheet("color: gray;");
        }
    }
}

void ClaudeView::onApiKeyChanged() {
    // Save API key to controller immediately so it persists
    if (m_claudeController) {
        m_claudeController->setApiKey(m_apiKeyEdit->text().trimmed());
    }
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

void ClaudeView::onClaudeNotesChanged() {
    if (!m_claudeController) return;
    
    m_notesList->clear();
    
    QList<ClaudeNote> notes = m_claudeController->getNotes();
    for (const ClaudeNote& note : notes) {
        QString noteText = QString("[%1] #%2: %3").arg(note.timestamp).arg(note.id).arg(note.content);
        m_notesList->addItem(noteText);
    }
    
    // Auto-scroll to bottom to show latest notes
    m_notesList->scrollToBottom();
}

void ClaudeView::onClaudeErrorOccurred(const QString& error) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString errorMsg = QString("[%1] ERROR: %2").arg(timestamp, error);
    
    m_responseText->append(QString("<span style='color: orange;'>%1</span>").arg(errorMsg));
    m_responseText->append("");
    
    // Update error count display
    if (m_claudeController) {
        int errorCount = m_claudeController->getConsecutiveErrors();
        m_errorCountLabel->setText(QString::number(errorCount));
        if (errorCount > 0) {
            m_errorCountLabel->setStyleSheet("color: orange; font-weight: bold;");
        }
    }
    
    m_statusLabel->setText("Error (retrying...)");
    m_statusLabel->setStyleSheet("color: orange; font-weight: bold;");
    
    // Auto-scroll to bottom
    QScrollBar* scrollBar = m_responseText->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void ClaudeView::onClaudeCriticalError(const QString& error, const QString& errorCode) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString errorMsg = QString("[%1] CRITICAL ERROR [%2]: %3").arg(timestamp, errorCode, error);
    
    m_responseText->append(QString("<span style='color: red; font-weight: bold;'>%1</span>").arg(errorMsg));
    m_responseText->append("");
    m_responseText->append("<span style='color: gray;'>Game state has been saved to slot 9.</span>");
    m_responseText->append("");
    
    // Update UI to reflect stopped state
    m_startStopButton->setText("Start Claude");
    m_statusLabel->setText("Stopped (Error)");
    m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
    
    // Update error count
    m_errorCountLabel->setText("CRITICAL");
    m_errorCountLabel->setStyleSheet("color: red; font-weight: bold;");
    
    // Auto-scroll to bottom
    QScrollBar* scrollBar = m_responseText->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void ClaudeView::onLoopTick() {
    m_loopCount++;
    m_loopCountLabel->setText(QString::number(m_loopCount));
    
    // Reset error styling on successful tick
    if (m_claudeController && m_claudeController->getConsecutiveErrors() == 0) {
        m_errorCountLabel->setText("0");
        m_errorCountLabel->setStyleSheet("color: gray;");
        m_statusLabel->setText("Running");
        m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
    }
}

void ClaudeView::updateStatus() {
    // Additional periodic status updates can go here
    updateButtonStates();
}

void ClaudeView::updateButtonStates() {
    bool hasApiKey = !m_apiKeyEdit->text().trimmed().isEmpty();
    bool hasController = m_claudeController != nullptr;
    bool canStart = hasController && m_claudeController->canStart();
    
    m_startStopButton->setEnabled(hasApiKey && hasController && canStart);
    
    if (hasController && m_claudeController->isRunning()) {
        m_startStopButton->setText("Stop Claude");
        m_statusLabel->setText("Running");
        m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
    } else {
        m_startStopButton->setText("Start Claude");
        if (!canStart) {
            m_statusLabel->setText("Load a ROM first");
            m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
        } else if (!hasApiKey) {
            m_statusLabel->setText("Enter API key");
            m_statusLabel->setStyleSheet("color: orange; font-weight: bold;");
        } else {
            m_statusLabel->setText("Ready");
            m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
        }
    }
}

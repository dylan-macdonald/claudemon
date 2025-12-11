/* Copyright (c) 2024 Claudemon Project
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QGroupBox>
#include <QListWidget>
#include <QTimer>
#include <QComboBox>
#include <QCheckBox>

#include "ClaudeController.h"

namespace QGBA {

class ClaudeView : public QWidget {
Q_OBJECT

public:
    ClaudeView(QWidget* parent = nullptr);
    ~ClaudeView() = default;

    void setClaudeController(ClaudeController* controller);

private slots:
    void onStartStopClicked();
    void onApiKeyChanged();
    void onClaudeResponseReceived(const QString& response);
    void onClaudeInputsGenerated(const QList<ClaudeInput>& inputs);
    void onClaudeNotesChanged();
    void onClaudeErrorOccurred(const QString& error);
    void onClaudeCriticalError(const QString& error, const QString& errorCode);
    void onLoopTick();
    void updateStatus();
    void onClearNotesClicked();

private:
    void setupUI();
    void updateButtonStates();

    ClaudeController* m_claudeController;

    // UI Components
    QVBoxLayout* m_mainLayout;
    
    // API Key section
    QGroupBox* m_apiGroup;
    QLineEdit* m_apiKeyEdit;
    QPushButton* m_startStopButton;
    QLabel* m_statusLabel;
    QComboBox* m_modelCombo;
    QCheckBox* m_thinkingCheck;
    QCheckBox* m_webSearchCheck;
    
    // Claude Response section
    QGroupBox* m_responseGroup;
    QTextEdit* m_responseText;
    
    // Input History section
    QGroupBox* m_inputGroup;
    QListWidget* m_inputList;
    
    // Notes section
    QGroupBox* m_notesGroup;
    QListWidget* m_notesList;
    QPushButton* m_clearNotesButton;
    
    // Status section
    QGroupBox* m_statusGroup;
    QLabel* m_loopCountLabel;
    QLabel* m_lastActionLabel;
    QLabel* m_errorCountLabel;
    
    QTimer* m_statusTimer;
    int m_loopCount;
};

}

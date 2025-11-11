/* Copyright (c) 2025 mGBA Claude Integration
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "ClaudeController.h"
#include "CoreController.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>

#include <mgba/core/core.h>
#include <mgba/feature/claude/api-client.h>
#include <mgba/feature/claude/ai-player.h>

namespace QGBA {

// Settings Dialog Implementation
ClaudeSettingsDialog::ClaudeSettingsDialog(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Claude AI Settings"));
	setMinimumWidth(500);

	QVBoxLayout* layout = new QVBoxLayout(this);
	QFormLayout* formLayout = new QFormLayout();

	m_apiKey = new QLineEdit();
	m_apiKey->setEchoMode(QLineEdit::Password);
	m_apiKey->setPlaceholderText(tr("sk-ant-..."));
	formLayout->addRow(tr("API Key:"), m_apiKey);

	m_framesPerQuery = new QSpinBox();
	m_framesPerQuery->setMinimum(1);
	m_framesPerQuery->setMaximum(600);
	m_framesPerQuery->setValue(60);
	m_framesPerQuery->setSuffix(tr(" frames"));
	formLayout->addRow(tr("Frames Per Query:"), m_framesPerQuery);

	m_includeScreenshot = new QCheckBox(tr("Include screenshot in requests"));
	m_includeScreenshot->setChecked(true);
	formLayout->addRow(tr("Screenshot:"), m_includeScreenshot);

	m_includeRAM = new QCheckBox(tr("Include RAM data in requests"));
	m_includeRAM->setChecked(true);
	formLayout->addRow(tr("RAM Data:"), m_includeRAM);

	layout->addLayout(formLayout);

	QLabel* helpLabel = new QLabel(
		tr("<b>Note:</b> Frames per query determines how often Claude is queried. "
		   "60 frames = 1 second at normal speed. Lower values increase API costs.")
	);
	helpLabel->setWordWrap(true);
	layout->addWidget(helpLabel);

	QDialogButtonBox* buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel
	);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

// Control Widget Implementation
ClaudeControlWidget::ClaudeControlWidget(CoreController* controller, QWidget* parent)
	: QWidget(parent)
	, m_controller(controller)
	, m_aiPlayer(nullptr)
	, m_running(false)
{
	setWindowTitle(tr("Claude AI Player"));
	setMinimumSize(600, 400);

	// Initialize config with defaults
	ClaudeConfigDefault(&m_config);

	// Determine config path
	QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
	QDir dir(configDir);
	if (!dir.exists()) {
		dir.mkpath(".");
	}
	m_configPath = dir.filePath("claude-config.json");

	// Load config
	loadConfig();

	// Setup UI
	QVBoxLayout* layout = new QVBoxLayout(this);

	// Control buttons
	QHBoxLayout* controlLayout = new QHBoxLayout();

	m_toggleButton = new QPushButton(tr("Start AI"));
	m_toggleButton->setCheckable(true);
	connect(m_toggleButton, &QPushButton::clicked, this, &ClaudeControlWidget::toggleAI);

	m_settingsButton = new QPushButton(tr("Settings"));
	connect(m_settingsButton, &QPushButton::clicked, this, &ClaudeControlWidget::showSettings);

	m_clearLogButton = new QPushButton(tr("Clear Log"));
	connect(m_clearLogButton, &QPushButton::clicked, this, &ClaudeControlWidget::clearLog);

	controlLayout->addWidget(m_toggleButton);
	controlLayout->addWidget(m_settingsButton);
	controlLayout->addWidget(m_clearLogButton);
	controlLayout->addStretch();

	layout->addLayout(controlLayout);

	// Log widget
	QLabel* logLabel = new QLabel(tr("AI Activity Log:"));
	layout->addWidget(logLabel);

	m_logWidget = new QTextEdit();
	m_logWidget->setReadOnly(true);
	m_logWidget->setLineWrapMode(QTextEdit::WidgetWidth);
	layout->addWidget(m_logWidget);

	// Initial message
	m_logWidget->append(tr("Claude AI Player ready. Click 'Start AI' to begin."));
	m_logWidget->append(tr("Make sure to configure your API key in Settings first."));

	// Connect to frame advance signal if controller is set
	if (m_controller) {
		connect(m_controller, &CoreController::frameAvailable, this, &ClaudeControlWidget::onFrameAdvance);
	}
}

ClaudeControlWidget::~ClaudeControlWidget() {
	cleanupAIPlayer();
}

void ClaudeControlWidget::setController(CoreController* controller) {
	if (m_controller) {
		disconnect(m_controller, &CoreController::frameAvailable, this, &ClaudeControlWidget::onFrameAdvance);
	}

	m_controller = controller;

	if (m_controller) {
		connect(m_controller, &CoreController::frameAvailable, this, &ClaudeControlWidget::onFrameAdvance);
	}

	// Recreate AI player if running
	if (m_running) {
		cleanupAIPlayer();
		setupAIPlayer();
	}
}

void ClaudeControlWidget::loadConfig() {
	if (!ClaudeConfigLoad(&m_config, m_configPath.toUtf8().constData())) {
		// Use defaults if load fails
		ClaudeConfigDefault(&m_config);
		m_logWidget->append(tr("Using default configuration. Please set your API key in Settings."));
	} else {
		m_logWidget->append(tr("Configuration loaded from %1").arg(m_configPath));
	}
}

void ClaudeControlWidget::saveConfig() {
	if (!ClaudeConfigSave(&m_config, m_configPath.toUtf8().constData())) {
		m_logWidget->append(tr("Warning: Failed to save configuration"));
	} else {
		m_logWidget->append(tr("Configuration saved to %1").arg(m_configPath));
	}
}

void ClaudeControlWidget::setupAIPlayer() {
	if (!m_controller || !m_controller->thread() || !m_controller->thread()->core) {
		m_logWidget->append(tr("Error: No game loaded"));
		return;
	}

	mCore* core = m_controller->thread()->core;
	m_aiPlayer = ClaudeAIPlayerCreate(core, &m_config);

	if (!m_aiPlayer) {
		m_logWidget->append(tr("Error: Failed to create AI player"));
		return;
	}

	// Set log callback
	ClaudeAIPlayerSetLogCallback(m_aiPlayer, &ClaudeControlWidget::logCallback, this);

	// Start AI
	if (!ClaudeAIPlayerStart(m_aiPlayer)) {
		const char* error = ClaudeAIPlayerGetLastError(m_aiPlayer);
		m_logWidget->append(tr("Error: %1").arg(error ? error : "Unknown error"));
		cleanupAIPlayer();
		return;
	}

	m_logWidget->append(tr("AI player initialized successfully"));
}

void ClaudeControlWidget::cleanupAIPlayer() {
	if (m_aiPlayer) {
		ClaudeAIPlayerStop(m_aiPlayer);
		ClaudeAIPlayerDestroy(m_aiPlayer);
		m_aiPlayer = nullptr;
	}
}

void ClaudeControlWidget::toggleAI() {
	if (m_running) {
		// Stop AI
		cleanupAIPlayer();
		m_running = false;
		m_toggleButton->setText(tr("Start AI"));
		m_toggleButton->setChecked(false);
		m_settingsButton->setEnabled(true);
		m_logWidget->append(tr("--- AI Stopped ---"));
		emit aiStateChanged(false);
	} else {
		// Check if API key is set
		if (strlen(m_config.apiKey) == 0) {
			QMessageBox::warning(this, tr("API Key Required"),
				tr("Please configure your Claude API key in Settings before starting the AI."));
			m_toggleButton->setChecked(false);
			return;
		}

		// Start AI
		setupAIPlayer();
		if (m_aiPlayer && ClaudeAIPlayerGetState(m_aiPlayer) == CLAUDE_AI_RUNNING) {
			m_running = true;
			m_toggleButton->setText(tr("Stop AI"));
			m_toggleButton->setChecked(true);
			m_settingsButton->setEnabled(false);
			m_logWidget->append(tr("--- AI Started ---"));
			emit aiStateChanged(true);
		} else {
			m_toggleButton->setChecked(false);
		}
	}
}

void ClaudeControlWidget::showSettings() {
	ClaudeSettingsDialog dialog(this);

	// Load current settings
	dialog.setApiKey(QString::fromUtf8(m_config.apiKey));
	dialog.setFramesPerQuery(m_config.framesPerQuery);
	dialog.setIncludeScreenshot(m_config.includeScreenshot);
	dialog.setIncludeRAM(m_config.includeRAM);

	if (dialog.exec() == QDialog::Accepted) {
		// Save new settings
		strncpy(m_config.apiKey, dialog.apiKey().toUtf8().constData(), sizeof(m_config.apiKey) - 1);
		m_config.apiKey[sizeof(m_config.apiKey) - 1] = '\0';
		m_config.framesPerQuery = dialog.framesPerQuery();
		m_config.includeScreenshot = dialog.includeScreenshot();
		m_config.includeRAM = dialog.includeRAM();

		saveConfig();

		// Update AI player if running
		if (m_aiPlayer) {
			ClaudeAIPlayerUpdateConfig(m_aiPlayer, &m_config);
		}

		m_logWidget->append(tr("Settings updated"));
	}
}

void ClaudeControlWidget::clearLog() {
	m_logWidget->clear();
	m_logWidget->append(tr("Log cleared"));
}

void ClaudeControlWidget::onFrameAdvance() {
	if (m_running && m_aiPlayer) {
		ClaudeAIPlayerFrameCallback(m_aiPlayer);
	}
}

void ClaudeControlWidget::updateUI() {
	// Update UI based on state
}

void ClaudeControlWidget::logCallback(const char* message, void* context) {
	ClaudeControlWidget* widget = static_cast<ClaudeControlWidget*>(context);
	if (widget) {
		QString msg = QString::fromUtf8(message);
		widget->m_logWidget->append(msg);

		// Auto-scroll to bottom
		QTextCursor cursor = widget->m_logWidget->textCursor();
		cursor.movePosition(QTextCursor::End);
		widget->m_logWidget->setTextCursor(cursor);

		emit widget->logMessage(msg);
	}
}

// ClaudeController Implementation
ClaudeController::ClaudeController(CoreController* controller, QObject* parent)
	: QObject(parent)
	, m_controller(controller)
	, m_widget(nullptr)
{
	m_widget = new ClaudeControlWidget(controller);
}

ClaudeController::~ClaudeController() {
	if (m_widget) {
		delete m_widget;
	}
}

}

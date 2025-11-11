/* Copyright (c) 2025 mGBA Claude Integration
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QObject>
#include <QWidget>
#include <QDialog>
#include <QTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>

#include <mgba/feature/claude/ai-player.h>

namespace QGBA {

class CoreController;

class ClaudeSettingsDialog : public QDialog {
Q_OBJECT

public:
	ClaudeSettingsDialog(QWidget* parent = nullptr);

	QString apiKey() const { return m_apiKey->text(); }
	void setApiKey(const QString& key) { m_apiKey->setText(key); }

	int framesPerQuery() const { return m_framesPerQuery->value(); }
	void setFramesPerQuery(int frames) { m_framesPerQuery->setValue(frames); }

	bool includeScreenshot() const { return m_includeScreenshot->isChecked(); }
	void setIncludeScreenshot(bool include) { m_includeScreenshot->setChecked(include); }

	bool includeRAM() const { return m_includeRAM->isChecked(); }
	void setIncludeRAM(bool include) { m_includeRAM->setChecked(include); }

private:
	QLineEdit* m_apiKey;
	QSpinBox* m_framesPerQuery;
	QCheckBox* m_includeScreenshot;
	QCheckBox* m_includeRAM;
};

class ClaudeControlWidget : public QWidget {
Q_OBJECT

public:
	ClaudeControlWidget(CoreController* controller, QWidget* parent = nullptr);
	~ClaudeControlWidget();

	void setController(CoreController* controller);

public slots:
	void toggleAI();
	void showSettings();
	void clearLog();
	void onFrameAdvance();

signals:
	void aiStateChanged(bool running);
	void logMessage(const QString& message);

private:
	void loadConfig();
	void saveConfig();
	void updateUI();
	void setupAIPlayer();
	void cleanupAIPlayer();

	static void logCallback(const char* message, void* context);

	CoreController* m_controller;
	ClaudeAIPlayer* m_aiPlayer;
	ClaudeConfig m_config;

	QPushButton* m_toggleButton;
	QPushButton* m_settingsButton;
	QPushButton* m_clearLogButton;
	QTextEdit* m_logWidget;

	bool m_running;
	QString m_configPath;
};

class ClaudeController : public QObject {
Q_OBJECT

public:
	explicit ClaudeController(CoreController* controller, QObject* parent = nullptr);
	~ClaudeController();

	ClaudeControlWidget* widget() { return m_widget; }

private:
	CoreController* m_controller;
	ClaudeControlWidget* m_widget;
};

}

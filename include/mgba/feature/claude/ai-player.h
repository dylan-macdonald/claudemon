/* Copyright (c) 2025 mGBA Claude Integration
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef MGBA_FEATURE_CLAUDE_AI_PLAYER_H
#define MGBA_FEATURE_CLAUDE_AI_PLAYER_H

#include <mgba-util/common.h>

CXX_GUARD_START

#include <mgba/core/core.h>
#include <mgba/feature/claude/api-client.h>

struct ClaudeAIPlayer;

enum ClaudeAIPlayerState {
	CLAUDE_AI_STOPPED,
	CLAUDE_AI_RUNNING,
	CLAUDE_AI_PAUSED,
	CLAUDE_AI_ERROR
};

struct ClaudeGameState {
	char gameName[16];
	char gameCode[8];
	uint32_t frameNumber;
	uint8_t* screenshot;
	size_t screenshotSize;
	uint8_t* ramData;
	size_t ramDataSize;
	char* ramHexDump;
};

// Callback for logging AI thoughts/actions
typedef void (*ClaudeLogCallback)(const char* message, void* context);

// Create and destroy AI player
struct ClaudeAIPlayer* ClaudeAIPlayerCreate(struct mCore* core, const struct ClaudeConfig* config);
void ClaudeAIPlayerDestroy(struct ClaudeAIPlayer* player);

// Control AI player
bool ClaudeAIPlayerStart(struct ClaudeAIPlayer* player);
void ClaudeAIPlayerStop(struct ClaudeAIPlayer* player);
void ClaudeAIPlayerPause(struct ClaudeAIPlayer* player);
void ClaudeAIPlayerResume(struct ClaudeAIPlayer* player);

// Get state
enum ClaudeAIPlayerState ClaudeAIPlayerGetState(struct ClaudeAIPlayer* player);
const char* ClaudeAIPlayerGetLastError(struct ClaudeAIPlayer* player);
const char* ClaudeAIPlayerGetLastResponse(struct ClaudeAIPlayer* player);

// Set callback for logging
void ClaudeAIPlayerSetLogCallback(struct ClaudeAIPlayer* player, ClaudeLogCallback callback, void* context);

// Update configuration
void ClaudeAIPlayerUpdateConfig(struct ClaudeAIPlayer* player, const struct ClaudeConfig* config);

// Frame callback (called by core on each frame)
void ClaudeAIPlayerFrameCallback(struct ClaudeAIPlayer* player);

// Extract game state
struct ClaudeGameState* ClaudeExtractGameState(struct mCore* core, bool includeScreenshot, bool includeRAM);
void ClaudeGameStateDestroy(struct ClaudeGameState* state);

// Parse button inputs from Claude response
uint16_t ClaudeParseButtons(const char* response, int* holdFrames);

// Button name to key mapping
const char* ClaudeGetButtonName(int keyId);

CXX_GUARD_END

#endif

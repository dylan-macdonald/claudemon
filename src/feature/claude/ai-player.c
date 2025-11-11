/* Copyright (c) 2025 mGBA Claude Integration
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/feature/claude/ai-player.h>

#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/png-io.h>
#include <mgba-util/vfs.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

struct ClaudeAIPlayer {
	struct mCore* core;
	struct ClaudeConfig config;
	struct ClaudeAPIClient* client;

	enum ClaudeAIPlayerState state;
	uint32_t frameCounter;
	uint32_t lastQueryFrame;

	char* lastResponse;
	char* lastError;

	// Button state
	uint16_t currentButtons;
	int buttonHoldFrames;
	int buttonHoldCounter;

	// Logging
	ClaudeLogCallback logCallback;
	void* logContext;
};

static void ClaudeLog(struct ClaudeAIPlayer* player, const char* format, ...) {
	if (!player->logCallback) {
		return;
	}

	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	player->logCallback(buffer, player->logContext);
}

struct ClaudeAIPlayer* ClaudeAIPlayerCreate(struct mCore* core, const struct ClaudeConfig* config) {
	if (!core || !config) {
		return NULL;
	}

	struct ClaudeAIPlayer* player = calloc(1, sizeof(struct ClaudeAIPlayer));
	if (!player) {
		return NULL;
	}

	player->core = core;
	memcpy(&player->config, config, sizeof(struct ClaudeConfig));

	player->client = ClaudeAPIClientCreate(config);
	if (!player->client) {
		free(player);
		return NULL;
	}

	player->state = CLAUDE_AI_STOPPED;
	player->frameCounter = 0;
	player->lastQueryFrame = 0;

	return player;
}

void ClaudeAIPlayerDestroy(struct ClaudeAIPlayer* player) {
	if (!player) {
		return;
	}

	ClaudeAPIClientDestroy(player->client);
	free(player->lastResponse);
	free(player->lastError);
	free(player);
}

bool ClaudeAIPlayerStart(struct ClaudeAIPlayer* player) {
	if (!player) {
		return false;
	}

	if (player->config.apiKey[0] == '\0') {
		player->state = CLAUDE_AI_ERROR;
		player->lastError = strdup("API key not configured");
		return false;
	}

	player->state = CLAUDE_AI_RUNNING;
	player->frameCounter = 0;
	player->lastQueryFrame = 0;
	player->currentButtons = 0;
	player->buttonHoldCounter = 0;

	ClaudeLog(player, "Claude AI Player started");
	return true;
}

void ClaudeAIPlayerStop(struct ClaudeAIPlayer* player) {
	if (!player) {
		return;
	}

	player->state = CLAUDE_AI_STOPPED;
	player->currentButtons = 0;

	// Clear any held buttons
	player->core->clearKeys(player->core, 0xFFFF);

	ClaudeLog(player, "Claude AI Player stopped");
}

void ClaudeAIPlayerPause(struct ClaudeAIPlayer* player) {
	if (!player || player->state != CLAUDE_AI_RUNNING) {
		return;
	}

	player->state = CLAUDE_AI_PAUSED;
	ClaudeLog(player, "Claude AI Player paused");
}

void ClaudeAIPlayerResume(struct ClaudeAIPlayer* player) {
	if (!player || player->state != CLAUDE_AI_PAUSED) {
		return;
	}

	player->state = CLAUDE_AI_RUNNING;
	ClaudeLog(player, "Claude AI Player resumed");
}

enum ClaudeAIPlayerState ClaudeAIPlayerGetState(struct ClaudeAIPlayer* player) {
	return player ? player->state : CLAUDE_AI_STOPPED;
}

const char* ClaudeAIPlayerGetLastError(struct ClaudeAIPlayer* player) {
	return player ? player->lastError : NULL;
}

const char* ClaudeAIPlayerGetLastResponse(struct ClaudeAIPlayer* player) {
	return player ? player->lastResponse : NULL;
}

void ClaudeAIPlayerSetLogCallback(struct ClaudeAIPlayer* player, ClaudeLogCallback callback, void* context) {
	if (!player) {
		return;
	}

	player->logCallback = callback;
	player->logContext = context;
}

void ClaudeAIPlayerUpdateConfig(struct ClaudeAIPlayer* player, const struct ClaudeConfig* config) {
	if (!player || !config) {
		return;
	}

	memcpy(&player->config, config, sizeof(struct ClaudeConfig));

	// Recreate client with new config
	ClaudeAPIClientDestroy(player->client);
	player->client = ClaudeAPIClientCreate(config);
}

struct ClaudeGameState* ClaudeExtractGameState(struct mCore* core, bool includeScreenshot, bool includeRAM) {
	if (!core) {
		return NULL;
	}

	struct ClaudeGameState* state = calloc(1, sizeof(struct ClaudeGameState));
	if (!state) {
		return NULL;
	}

	// Get game name from ROM header
	// GBA ROM header is at 0x080000A0
	uint32_t romBase = 0x08000000;

	// Read game title (12 bytes at offset 0xA0)
	for (int i = 0; i < 12; i++) {
		uint8_t byte = core->busRead8(core, romBase + 0xA0 + i);
		if (byte >= 32 && byte < 127) {
			state->gameName[i] = byte;
		} else {
			state->gameName[i] = ' ';
		}
	}
	state->gameName[12] = '\0';

	// Read game code (4 bytes at offset 0xAC)
	for (int i = 0; i < 4; i++) {
		state->gameCode[i] = core->busRead8(core, romBase + 0xAC + i);
	}
	state->gameCode[4] = '\0';

	state->frameNumber = core->frameCounter(core);

	// Capture screenshot
	if (includeScreenshot) {
		// Create a memory VFile for PNG output
		struct VFile* vf = VFileMemChunk(NULL, 0);
		if (vf) {
			// Get video dimensions
			unsigned width, height;
			core->desiredVideoDimensions(core, &width, &height);

			// Allocate framebuffer
			uint32_t* pixels = malloc(width * height * sizeof(uint32_t));
			if (pixels) {
				core->getPixels(core, pixels, width);

				// Convert to PNG and encode
				if (PNGWritePixels(vf, width, height, width, pixels, true)) {
					size_t size = vf->seek(vf, 0, SEEK_END);
					vf->seek(vf, 0, SEEK_SET);

					state->screenshot = malloc(size);
					if (state->screenshot) {
						vf->read(vf, state->screenshot, size);
						state->screenshotSize = size;
					}
				}

				free(pixels);
			}

			vf->close(vf);
		}
	}

	// Read RAM regions
	if (includeRAM) {
		// Read EWRAM (256KB at 0x02000000) and IWRAM (32KB at 0x03000000)
		size_t ewramSize = 256 * 1024;
		size_t iwramSize = 32 * 1024;
		state->ramDataSize = ewramSize + iwramSize;
		state->ramData = malloc(state->ramDataSize);

		if (state->ramData) {
			// Read EWRAM
			for (size_t i = 0; i < ewramSize; i++) {
				state->ramData[i] = core->busRead8(core, 0x02000000 + i);
			}

			// Read IWRAM
			for (size_t i = 0; i < iwramSize; i++) {
				state->ramData[ewramSize + i] = core->busRead8(core, 0x03000000 + i);
			}

			// Create hex dump (first 1KB of EWRAM for brevity)
			size_t dumpSize = 1024 < ewramSize ? 1024 : ewramSize;
			state->ramHexDump = malloc(dumpSize * 3 + 256); // 3 chars per byte + header
			if (state->ramHexDump) {
				int offset = sprintf(state->ramHexDump, "RAM (first 1KB of EWRAM):\n");
				for (size_t i = 0; i < dumpSize; i++) {
					if (i % 16 == 0) {
						offset += sprintf(state->ramHexDump + offset, "\n%08X: ", (uint32_t)(0x02000000 + i));
					}
					offset += sprintf(state->ramHexDump + offset, "%02X ", state->ramData[i]);
				}
				state->ramHexDump[offset] = '\0';
			}
		}
	}

	return state;
}

void ClaudeGameStateDestroy(struct ClaudeGameState* state) {
	if (!state) {
		return;
	}

	free(state->screenshot);
	free(state->ramData);
	free(state->ramHexDump);
	free(state);
}

static char* BuildPrompt(struct ClaudeGameState* state) {
	size_t bufferSize = 4096;
	if (state->ramHexDump) {
		bufferSize += strlen(state->ramHexDump);
	}

	char* prompt = malloc(bufferSize);
	if (!prompt) {
		return NULL;
	}

	int offset = 0;

	offset += snprintf(prompt + offset, bufferSize - offset,
		"You are playing %s (Game Code: %s) on a Game Boy Advance emulator.\n\n"
		"Current Frame: %u\n\n",
		state->gameName,
		state->gameCode,
		state->frameNumber
	);

	if (state->ramHexDump) {
		offset += snprintf(prompt + offset, bufferSize - offset,
			"%s\n\n",
			state->ramHexDump
		);
	}

	offset += snprintf(prompt + offset, bufferSize - offset,
		"Available buttons: A, B, START, SELECT, UP, DOWN, LEFT, RIGHT, L, R\n\n"
		"You can also combine buttons with + (e.g., 'UP+A' to press both simultaneously)\n\n"
		"Analyze the screenshot and game state. What should you do next?\n"
		"Think step by step about the current situation and your goal.\n\n"
		"Respond with your reasoning, then specify button presses in this format:\n"
		"BUTTONS: <comma-separated list of buttons to press>\n"
		"HOLD_FRAMES: <number of frames to hold the buttons, default 10>\n\n"
		"Example responses:\n"
		"BUTTONS: A\n"
		"HOLD_FRAMES: 15\n\n"
		"Or to move and press A:\n"
		"BUTTONS: UP+A\n"
		"HOLD_FRAMES: 5\n\n"
		"Or to do nothing:\n"
		"BUTTONS: NONE\n"
	);

	return prompt;
}

uint16_t ClaudeParseButtons(const char* response, int* holdFrames) {
	if (!response) {
		return 0;
	}

	// Default hold frames
	*holdFrames = 10;

	// Find BUTTONS: line
	const char* buttonsLine = strstr(response, "BUTTONS:");
	if (!buttonsLine) {
		return 0;
	}

	buttonsLine += 8; // strlen("BUTTONS:")
	while (*buttonsLine == ' ' || *buttonsLine == '\t') {
		buttonsLine++;
	}

	// Parse button names
	uint16_t buttons = 0;
	char buttonBuffer[64];
	int bufIdx = 0;

	while (*buttonsLine && *buttonsLine != '\n' && *buttonsLine != '\r') {
		char c = toupper(*buttonsLine);

		if (c == ',' || c == ' ' || c == '\t') {
			if (bufIdx > 0) {
				buttonBuffer[bufIdx] = '\0';

				// Parse the button combination (may have + for multiple)
				char* token = strtok(buttonBuffer, "+");
				while (token) {
					// Trim whitespace
					while (*token == ' ') token++;
					char* end = token + strlen(token) - 1;
					while (end > token && *end == ' ') *end-- = '\0';

					// Map button names to GBA keys
					if (strcmp(token, "A") == 0) buttons |= (1 << 0);
					else if (strcmp(token, "B") == 0) buttons |= (1 << 1);
					else if (strcmp(token, "SELECT") == 0) buttons |= (1 << 2);
					else if (strcmp(token, "START") == 0) buttons |= (1 << 3);
					else if (strcmp(token, "RIGHT") == 0) buttons |= (1 << 4);
					else if (strcmp(token, "LEFT") == 0) buttons |= (1 << 5);
					else if (strcmp(token, "UP") == 0) buttons |= (1 << 6);
					else if (strcmp(token, "DOWN") == 0) buttons |= (1 << 7);
					else if (strcmp(token, "R") == 0) buttons |= (1 << 8);
					else if (strcmp(token, "L") == 0) buttons |= (1 << 9);

					token = strtok(NULL, "+");
				}

				bufIdx = 0;
			}
		} else if (c != '+') {
			if (bufIdx < sizeof(buttonBuffer) - 1) {
				buttonBuffer[bufIdx++] = c;
			}
		}

		buttonsLine++;
	}

	// Process last button if any
	if (bufIdx > 0) {
		buttonBuffer[bufIdx] = '\0';
		char* token = strtok(buttonBuffer, "+");
		while (token) {
			while (*token == ' ') token++;
			char* end = token + strlen(token) - 1;
			while (end > token && *end == ' ') *end-- = '\0';

			if (strcmp(token, "A") == 0) buttons |= (1 << 0);
			else if (strcmp(token, "B") == 0) buttons |= (1 << 1);
			else if (strcmp(token, "SELECT") == 0) buttons |= (1 << 2);
			else if (strcmp(token, "START") == 0) buttons |= (1 << 3);
			else if (strcmp(token, "RIGHT") == 0) buttons |= (1 << 4);
			else if (strcmp(token, "LEFT") == 0) buttons |= (1 << 5);
			else if (strcmp(token, "UP") == 0) buttons |= (1 << 6);
			else if (strcmp(token, "DOWN") == 0) buttons |= (1 << 7);
			else if (strcmp(token, "R") == 0) buttons |= (1 << 8);
			else if (strcmp(token, "L") == 0) buttons |= (1 << 9);

			token = strtok(NULL, "+");
		}
	}

	// Find HOLD_FRAMES: line
	const char* holdLine = strstr(response, "HOLD_FRAMES:");
	if (holdLine) {
		holdLine += 12; // strlen("HOLD_FRAMES:")
		*holdFrames = atoi(holdLine);
		if (*holdFrames < 1) *holdFrames = 1;
		if (*holdFrames > 300) *holdFrames = 300; // Max 5 seconds at 60fps
	}

	return buttons;
}

void ClaudeAIPlayerFrameCallback(struct ClaudeAIPlayer* player) {
	if (!player || player->state != CLAUDE_AI_RUNNING) {
		return;
	}

	player->frameCounter++;

	// Handle button holds
	if (player->buttonHoldCounter > 0) {
		player->buttonHoldCounter--;
		if (player->buttonHoldCounter == 0) {
			// Release buttons
			player->core->clearKeys(player->core, player->currentButtons);
			player->currentButtons = 0;
		}
		return; // Don't query Claude while holding buttons
	}

	// Check if it's time to query Claude
	uint32_t framesSinceLastQuery = player->frameCounter - player->lastQueryFrame;
	if (framesSinceLastQuery < player->config.framesPerQuery) {
		return;
	}

	player->lastQueryFrame = player->frameCounter;

	// Extract game state
	struct ClaudeGameState* gameState = ClaudeExtractGameState(
		player->core,
		player->config.includeScreenshot,
		player->config.includeRAM
	);

	if (!gameState) {
		ClaudeLog(player, "Failed to extract game state");
		return;
	}

	ClaudeLog(player, "Frame %u: Querying Claude for next action...", player->frameCounter);

	// Build prompt
	char* prompt = BuildPrompt(gameState);
	if (!prompt) {
		ClaudeGameStateDestroy(gameState);
		return;
	}

	// Encode screenshot to base64
	char* screenshotBase64 = NULL;
	if (gameState->screenshot && gameState->screenshotSize > 0) {
		size_t base64Len;
		screenshotBase64 = ClaudeBase64Encode(gameState->screenshot, gameState->screenshotSize, &base64Len);
	}

	// Send to Claude
	struct ClaudeResponse* response = ClaudeAPISendMessage(
		player->client,
		prompt,
		screenshotBase64,
		gameState->screenshotSize
	);

	// Cleanup
	free(prompt);
	ClaudeBase64Free(screenshotBase64);
	ClaudeGameStateDestroy(gameState);

	if (!response) {
		ClaudeLog(player, "Failed to get response from Claude API");
		return;
	}

	if (!response->success) {
		ClaudeLog(player, "Claude API error: %s", response->errorMessage ? response->errorMessage : "Unknown");
		ClaudeResponseDestroy(response);
		return;
	}

	// Store response
	free(player->lastResponse);
	player->lastResponse = strdup(response->text);

	ClaudeLog(player, "Claude response: %s", response->text);

	// Parse buttons
	int holdFrames = 10;
	uint16_t buttons = ClaudeParseButtons(response->text, &holdFrames);

	if (buttons != 0) {
		player->currentButtons = buttons;
		player->buttonHoldFrames = holdFrames;
		player->buttonHoldCounter = holdFrames;

		// Press buttons
		player->core->setKeys(player->core, buttons);

		// Log which buttons
		char buttonStr[256] = {0};
		int offset = 0;
		if (buttons & (1 << 0)) offset += sprintf(buttonStr + offset, "A ");
		if (buttons & (1 << 1)) offset += sprintf(buttonStr + offset, "B ");
		if (buttons & (1 << 2)) offset += sprintf(buttonStr + offset, "SELECT ");
		if (buttons & (1 << 3)) offset += sprintf(buttonStr + offset, "START ");
		if (buttons & (1 << 4)) offset += sprintf(buttonStr + offset, "RIGHT ");
		if (buttons & (1 << 5)) offset += sprintf(buttonStr + offset, "LEFT ");
		if (buttons & (1 << 6)) offset += sprintf(buttonStr + offset, "UP ");
		if (buttons & (1 << 7)) offset += sprintf(buttonStr + offset, "DOWN ");
		if (buttons & (1 << 8)) offset += sprintf(buttonStr + offset, "R ");
		if (buttons & (1 << 9)) offset += sprintf(buttonStr + offset, "L ");

		ClaudeLog(player, "Pressing buttons: %s (hold %d frames)", buttonStr, holdFrames);
	}

	ClaudeResponseDestroy(response);
}

const char* ClaudeGetButtonName(int keyId) {
	switch (keyId) {
		case 0: return "A";
		case 1: return "B";
		case 2: return "SELECT";
		case 3: return "START";
		case 4: return "RIGHT";
		case 5: return "LEFT";
		case 6: return "UP";
		case 7: return "DOWN";
		case 8: return "R";
		case 9: return "L";
		default: return "UNKNOWN";
	}
}

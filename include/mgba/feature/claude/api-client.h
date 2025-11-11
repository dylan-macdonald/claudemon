/* Copyright (c) 2025 mGBA Claude Integration
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef MGBA_FEATURE_CLAUDE_API_CLIENT_H
#define MGBA_FEATURE_CLAUDE_API_CLIENT_H

#include <mgba-util/common.h>

CXX_GUARD_START

#include <stddef.h>
#include <stdbool.h>

struct ClaudeConfig {
	char apiKey[256];
	char model[128];
	int maxTokens;
	int framesPerQuery;
	bool includeScreenshot;
	bool includeRAM;
	float temperature;
};

struct ClaudeResponse {
	char* text;
	bool success;
	char* errorMessage;
	int tokensUsed;
};

struct ClaudeAPIClient;

// Create and destroy client
struct ClaudeAPIClient* ClaudeAPIClientCreate(const struct ClaudeConfig* config);
void ClaudeAPIClientDestroy(struct ClaudeAPIClient* client);

// Send message to Claude API
struct ClaudeResponse* ClaudeAPISendMessage(struct ClaudeAPIClient* client,
                                             const char* prompt,
                                             const char* imageBase64,
                                             size_t imageSize);

// Free response
void ClaudeResponseDestroy(struct ClaudeResponse* response);

// Utility functions
char* ClaudeBase64Encode(const unsigned char* data, size_t inputLength, size_t* outputLength);
void ClaudeBase64Free(char* encoded);

// Config management
bool ClaudeConfigLoad(struct ClaudeConfig* config, const char* filepath);
bool ClaudeConfigSave(const struct ClaudeConfig* config, const char* filepath);
void ClaudeConfigDefault(struct ClaudeConfig* config);

CXX_GUARD_END

#endif

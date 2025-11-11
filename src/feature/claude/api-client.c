/* Copyright (c) 2025 mGBA Claude Integration
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/feature/claude/api-client.h>

#include <mgba-util/string.h>
#include <mgba-util/vfs.h>

#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef USE_JSON_C
#include <json-c/json.h>
#else
// We'll implement basic JSON handling if json-c is not available
#endif

struct ClaudeAPIClient {
	struct ClaudeConfig config;
	CURL* curl;
	char errorBuffer[CURL_ERROR_SIZE];
};

// Buffer for CURL responses
struct MemoryBuffer {
	char* data;
	size_t size;
};

// Base64 encoding table
static const char base64_chars[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// CURL write callback
static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
	size_t realsize = size * nmemb;
	struct MemoryBuffer* mem = (struct MemoryBuffer*)userp;

	char* ptr = realloc(mem->data, mem->size + realsize + 1);
	if (!ptr) {
		return 0; // Out of memory
	}

	mem->data = ptr;
	memcpy(&(mem->data[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->data[mem->size] = 0;

	return realsize;
}

struct ClaudeAPIClient* ClaudeAPIClientCreate(const struct ClaudeConfig* config) {
	struct ClaudeAPIClient* client = calloc(1, sizeof(struct ClaudeAPIClient));
	if (!client) {
		return NULL;
	}

	memcpy(&client->config, config, sizeof(struct ClaudeConfig));

	client->curl = curl_easy_init();
	if (!client->curl) {
		free(client);
		return NULL;
	}

	curl_easy_setopt(client->curl, CURLOPT_ERRORBUFFER, client->errorBuffer);
	curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

	return client;
}

void ClaudeAPIClientDestroy(struct ClaudeAPIClient* client) {
	if (!client) {
		return;
	}

	if (client->curl) {
		curl_easy_cleanup(client->curl);
	}

	free(client);
}

char* ClaudeBase64Encode(const unsigned char* data, size_t inputLength, size_t* outputLength) {
	size_t outputSize = 4 * ((inputLength + 2) / 3);
	char* encoded = malloc(outputSize + 1);
	if (!encoded) {
		return NULL;
	}

	size_t i = 0, j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];
	int idx = 0;

	while (inputLength--) {
		char_array_3[idx++] = *(data++);
		if (idx == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (idx = 0; idx < 4; idx++) {
				encoded[j++] = base64_chars[char_array_4[idx]];
			}
			idx = 0;
		}
	}

	if (idx) {
		for (i = idx; i < 3; i++) {
			char_array_3[i] = '\0';
		}

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

		for (i = 0; i < idx + 1; i++) {
			encoded[j++] = base64_chars[char_array_4[i]];
		}

		while (idx++ < 3) {
			encoded[j++] = '=';
		}
	}

	encoded[j] = '\0';
	if (outputLength) {
		*outputLength = j;
	}

	return encoded;
}

void ClaudeBase64Free(char* encoded) {
	free(encoded);
}

// Simple JSON builder (avoiding external dependencies)
static char* BuildJSONRequest(const struct ClaudeConfig* config,
                               const char* prompt,
                               const char* imageBase64) {
	size_t bufferSize = 1024 * 1024; // 1MB initial buffer
	if (imageBase64) {
		bufferSize += strlen(imageBase64) + 1024;
	}

	char* json = malloc(bufferSize);
	if (!json) {
		return NULL;
	}

	int offset = 0;

	// Start JSON
	offset += snprintf(json + offset, bufferSize - offset,
		"{"
		"\"model\":\"%s\","
		"\"max_tokens\":%d,"
		"\"temperature\":%.2f,"
		"\"messages\":[{"
		"\"role\":\"user\","
		"\"content\":[",
		config->model,
		config->maxTokens,
		config->temperature
	);

	// Add image if provided
	if (imageBase64) {
		offset += snprintf(json + offset, bufferSize - offset,
			"{"
			"\"type\":\"image\","
			"\"source\":{"
			"\"type\":\"base64\","
			"\"media_type\":\"image/png\","
			"\"data\":\"%s\""
			"}},",
			imageBase64
		);
	}

	// Add text prompt - escape quotes in prompt
	offset += snprintf(json + offset, bufferSize - offset,
		"{"
		"\"type\":\"text\","
		"\"text\":\""
	);

	// Escape the prompt text
	for (const char* p = prompt; *p && offset < bufferSize - 10; p++) {
		if (*p == '"' || *p == '\\') {
			json[offset++] = '\\';
		}
		if (*p == '\n') {
			json[offset++] = '\\';
			json[offset++] = 'n';
		} else if (*p == '\t') {
			json[offset++] = '\\';
			json[offset++] = 't';
		} else {
			json[offset++] = *p;
		}
	}

	// Close JSON
	offset += snprintf(json + offset, bufferSize - offset,
		"\"}"
		"]"
		"}]"
		"}"
	);

	return json;
}

// Parse JSON response
static struct ClaudeResponse* ParseJSONResponse(const char* jsonText) {
	struct ClaudeResponse* response = calloc(1, sizeof(struct ClaudeResponse));
	if (!response) {
		return NULL;
	}

	// Simple JSON parsing - look for "text" field in content
	// Format: {"content":[{"type":"text","text":"..."}],...}

	const char* textStart = strstr(jsonText, "\"text\":\"");
	if (!textStart) {
		// Check for error
		const char* errorStart = strstr(jsonText, "\"error\"");
		if (errorStart) {
			const char* messageStart = strstr(errorStart, "\"message\":\"");
			if (messageStart) {
				messageStart += 11; // strlen("\"message\":\"")
				const char* messageEnd = strchr(messageStart, '"');
				if (messageEnd) {
					size_t len = messageEnd - messageStart;
					response->errorMessage = malloc(len + 1);
					if (response->errorMessage) {
						strncpy(response->errorMessage, messageStart, len);
						response->errorMessage[len] = '\0';
					}
				}
			}
		}
		response->success = false;
		return response;
	}

	textStart += 8; // strlen("\"text\":\"")

	// Find the end of the text field (accounting for escaped quotes)
	const char* textEnd = textStart;
	while (*textEnd) {
		if (*textEnd == '"' && *(textEnd - 1) != '\\') {
			break;
		}
		textEnd++;
	}

	size_t textLen = textEnd - textStart;
	response->text = malloc(textLen + 1);
	if (!response->text) {
		free(response);
		return NULL;
	}

	// Copy and unescape
	size_t j = 0;
	for (size_t i = 0; i < textLen; i++) {
		if (textStart[i] == '\\' && i + 1 < textLen) {
			i++;
			switch (textStart[i]) {
				case 'n': response->text[j++] = '\n'; break;
				case 't': response->text[j++] = '\t'; break;
				case 'r': response->text[j++] = '\r'; break;
				case '"': response->text[j++] = '"'; break;
				case '\\': response->text[j++] = '\\'; break;
				default: response->text[j++] = textStart[i]; break;
			}
		} else {
			response->text[j++] = textStart[i];
		}
	}
	response->text[j] = '\0';

	response->success = true;
	return response;
}

struct ClaudeResponse* ClaudeAPISendMessage(struct ClaudeAPIClient* client,
                                             const char* prompt,
                                             const char* imageBase64,
                                             size_t imageSize) {
	if (!client || !prompt) {
		return NULL;
	}

	// Build JSON request
	char* jsonRequest = BuildJSONRequest(&client->config, prompt, imageBase64);
	if (!jsonRequest) {
		struct ClaudeResponse* response = calloc(1, sizeof(struct ClaudeResponse));
		response->success = false;
		response->errorMessage = strdup("Failed to build JSON request");
		return response;
	}

	// Setup CURL request
	struct MemoryBuffer responseBuffer = {0};
	struct curl_slist* headers = NULL;

	char authHeader[512];
	snprintf(authHeader, sizeof(authHeader), "x-api-key: %s", client->config.apiKey);

	headers = curl_slist_append(headers, authHeader);
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

	curl_easy_setopt(client->curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
	curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, jsonRequest);
	curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, (void*)&responseBuffer);
	curl_easy_setopt(client->curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(client->curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, 60L);

	// Perform request
	CURLcode res = curl_easy_perform(client->curl);

	struct ClaudeResponse* response = NULL;
	if (res != CURLE_OK) {
		response = calloc(1, sizeof(struct ClaudeResponse));
		response->success = false;
		response->errorMessage = strdup(curl_easy_strerror(res));
	} else {
		long httpCode = 0;
		curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &httpCode);

		if (httpCode == 200) {
			response = ParseJSONResponse(responseBuffer.data);
		} else {
			response = calloc(1, sizeof(struct ClaudeResponse));
			response->success = false;

			char errorBuf[512];
			snprintf(errorBuf, sizeof(errorBuf), "HTTP error %ld: %s",
			         httpCode, responseBuffer.data ? responseBuffer.data : "Unknown error");
			response->errorMessage = strdup(errorBuf);
		}
	}

	// Cleanup
	curl_slist_free_all(headers);
	free(jsonRequest);
	free(responseBuffer.data);

	return response;
}

void ClaudeResponseDestroy(struct ClaudeResponse* response) {
	if (!response) {
		return;
	}

	free(response->text);
	free(response->errorMessage);
	free(response);
}

void ClaudeConfigDefault(struct ClaudeConfig* config) {
	memset(config, 0, sizeof(struct ClaudeConfig));
	strncpy(config->model, "claude-sonnet-4-5-20250929", sizeof(config->model) - 1);
	config->maxTokens = 1024;
	config->framesPerQuery = 60; // Once per second at 60fps
	config->includeScreenshot = true;
	config->includeRAM = true;
	config->temperature = 1.0f;
}

bool ClaudeConfigLoad(struct ClaudeConfig* config, const char* filepath) {
	struct VFile* file = VFileOpen(filepath, O_RDONLY);
	if (!file) {
		return false;
	}

	ssize_t size = file->size(file);
	char* buffer = malloc(size + 1);
	if (!buffer) {
		file->close(file);
		return false;
	}

	file->read(file, buffer, size);
	buffer[size] = '\0';
	file->close(file);

	// Simple parsing - look for key-value pairs
	ClaudeConfigDefault(config);

	char* apiKeyStart = strstr(buffer, "\"api_key\"");
	if (apiKeyStart) {
		char* valueStart = strchr(apiKeyStart, ':');
		if (valueStart) {
			valueStart = strchr(valueStart, '"');
			if (valueStart) {
				valueStart++;
				char* valueEnd = strchr(valueStart, '"');
				if (valueEnd) {
					size_t len = valueEnd - valueStart;
					if (len < sizeof(config->apiKey)) {
						strncpy(config->apiKey, valueStart, len);
						config->apiKey[len] = '\0';
					}
				}
			}
		}
	}

	char* modelStart = strstr(buffer, "\"model\"");
	if (modelStart) {
		char* valueStart = strchr(modelStart, ':');
		if (valueStart) {
			valueStart = strchr(valueStart, '"');
			if (valueStart) {
				valueStart++;
				char* valueEnd = strchr(valueStart, '"');
				if (valueEnd) {
					size_t len = valueEnd - valueStart;
					if (len < sizeof(config->model)) {
						strncpy(config->model, valueStart, len);
						config->model[len] = '\0';
					}
				}
			}
		}
	}

	char* framesStart = strstr(buffer, "\"frames_per_query\"");
	if (framesStart) {
		char* valueStart = strchr(framesStart, ':');
		if (valueStart) {
			config->framesPerQuery = atoi(valueStart + 1);
		}
	}

	free(buffer);
	return true;
}

bool ClaudeConfigSave(const struct ClaudeConfig* config, const char* filepath) {
	struct VFile* file = VFileOpen(filepath, O_WRONLY | O_CREAT | O_TRUNC);
	if (!file) {
		return false;
	}

	char buffer[2048];
	int len = snprintf(buffer, sizeof(buffer),
		"{\n"
		"  \"api_key\": \"%s\",\n"
		"  \"model\": \"%s\",\n"
		"  \"max_tokens\": %d,\n"
		"  \"frames_per_query\": %d,\n"
		"  \"include_screenshot\": %s,\n"
		"  \"include_ram\": %s,\n"
		"  \"temperature\": %.2f\n"
		"}\n",
		config->apiKey,
		config->model,
		config->maxTokens,
		config->framesPerQuery,
		config->includeScreenshot ? "true" : "false",
		config->includeRAM ? "true" : "false",
		config->temperature
	);

	file->write(file, buffer, len);
	file->close(file);

	return true;
}

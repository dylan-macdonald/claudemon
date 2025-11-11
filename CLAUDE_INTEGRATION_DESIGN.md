# Claude API Integration Design for mGBA

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     mGBA Qt Frontend                        │
│  ┌──────────────────────────────────────────────────────┐  │
│  │         ClaudeController (Qt Widget)                 │  │
│  │  - Enable/Disable AI                                 │  │
│  │  - Configure API Key                                 │  │
│  │  - Display AI thoughts/actions                       │  │
│  └────────────────────┬─────────────────────────────────┘  │
│                       │                                     │
│  ┌────────────────────▼─────────────────────────────────┐  │
│  │         ClaudeAIPlayer (Core Logic)                  │  │
│  │  - Frame callback hook                               │  │
│  │  - Game state extraction                             │  │
│  │  - Screenshot capture                                │  │
│  │  - RAM reading                                       │  │
│  │  - Input injection                                   │  │
│  └────────────────────┬─────────────────────────────────┘  │
│                       │                                     │
│  ┌────────────────────▼─────────────────────────────────┐  │
│  │         ClaudeAPIClient (HTTP Client)                │  │
│  │  - Anthropic API communication                       │  │
│  │  - Message formatting                                │  │
│  │  - Base64 encoding for screenshots                   │  │
│  │  - Error handling & retry logic                      │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## Component Details

### 1. ClaudeAPIClient
- **File**: `src/feature/claude/claude-api-client.cpp`
- **Purpose**: HTTP client for Anthropic Claude API
- **Dependencies**: libcurl (for HTTP requests), nlohmann/json (for JSON parsing)
- **Key Methods**:
  - `sendMessage(prompt, imageBase64)` → returns Claude's response
  - `encodeBase64(buffer, size)` → base64 encoding for screenshots
  - `parseButtonsFromResponse(response)` → extract button inputs from Claude

### 2. ClaudeAIPlayer
- **File**: `src/feature/claude/claude-ai-player.cpp`
- **Purpose**: Core game-playing logic, integrates with mCore
- **Key Methods**:
  - `onFrameCallback()` → called every frame, decides when to query Claude
  - `extractGameState()` → reads RAM, game name, screenshot
  - `sendToClaudeAndGetInput()` → formats prompt, gets response
  - `injectInput()` → simulates button presses
- **Configuration**:
  - `framesPerQuery` → how many frames between Claude queries (default: 60 = 1 sec)
  - `includeScreenshot` → whether to send screenshots (default: true)
  - `includeRAM` → whether to send RAM data (default: true)

### 3. ClaudeController (Qt)
- **File**: `src/platform/qt/ClaudeController.cpp`
- **Purpose**: Qt UI for controlling Claude integration
- **Features**:
  - Enable/Disable AI toggle
  - API Key configuration (stored in config)
  - Speed control (frames per query)
  - Log window showing Claude's reasoning
  - Manual override toggle

### 4. Game State Extraction

#### ROM Header Reading (Game Name)
```c
// GBA ROM header at 0x080000A0
struct GBAROMHeader {
    char title[12];        // Game title
    char gamecode[4];      // Game code (e.g., "BPEE" for Pokemon Emerald)
    char makercode[2];     // Maker code
    uint8_t version;       // Version
    // ...
};
```

#### RAM Regions to Send
- **EWRAM** (0x02000000 - 256KB): Working RAM, game variables
- **IWRAM** (0x03000000 - 32KB): Fast internal RAM
- **For Pokemon Emerald**: Send relevant memory regions (party data, position, flags)

#### Screenshot Capture
- Use existing `mCore->takeScreenshot()` API
- Capture to memory buffer instead of file
- Encode as PNG → base64
- Resolution: 240×160 (GBA native)

### 5. Claude Prompt Format

```json
{
  "model": "claude-sonnet-4-5-20250929",
  "max_tokens": 1024,
  "messages": [
    {
      "role": "user",
      "content": [
        {
          "type": "image",
          "source": {
            "type": "base64",
            "media_type": "image/png",
            "data": "<base64_screenshot>"
          }
        },
        {
          "type": "text",
          "text": "You are playing Pokemon Emerald on a Game Boy Advance emulator.

                   Current game state:
                   - Frame: 1234
                   - RAM data: <hex dump of relevant regions>

                   Available buttons: A, B, START, SELECT, UP, DOWN, LEFT, RIGHT, L, R

                   Analyze the screenshot and current game state. What should you do next?
                   Respond with your reasoning and then specify button presses in this format:

                   BUTTONS: <comma-separated list, e.g., 'A', 'UP', 'RIGHT+A'>
                   HOLD_FRAMES: <number of frames to hold buttons, default 10>"
        }
      ]
    }
  ]
}
```

## Implementation Plan

### Phase 1: Core Infrastructure
1. Create `src/feature/claude/` directory
2. Implement ClaudeAPIClient with libcurl
3. Add JSON dependency (nlohmann/json or use existing json-c)
4. Implement base64 encoding
5. Test API connection with simple request

### Phase 2: Game State Integration
1. Implement game name reading from ROM header
2. Implement RAM region extraction
3. Implement screenshot-to-memory capture
4. Create unified GameState struct

### Phase 3: AI Player Logic
1. Implement ClaudeAIPlayer class
2. Hook into frame callbacks
3. Implement decision logic (when to query Claude)
4. Implement input injection
5. Add error handling and rate limiting

### Phase 4: Qt Integration
1. Create ClaudeController Qt widget
2. Add menu item: "Tools → Claude AI Player"
3. Implement settings dialog
4. Add log window for Claude's reasoning
5. Integrate with existing mGBA settings system

### Phase 5: Build System
1. Update CMakeLists.txt with `USE_CLAUDE` option
2. Find/configure libcurl
3. Add nlohmann/json as third-party dependency
4. Create Windows .bat setup script

### Phase 6: Testing
1. Test with Pokemon Emerald ROM
2. Verify screenshot quality
3. Verify RAM reading accuracy
4. Test input injection
5. Monitor API costs and rate limits

## Configuration File

**Location**: `claude-config.json`
```json
{
  "api_key": "sk-ant-...",
  "model": "claude-sonnet-4-5-20250929",
  "frames_per_query": 60,
  "max_tokens": 1024,
  "include_screenshot": true,
  "include_ram": true,
  "ram_regions": [
    {"name": "EWRAM", "start": "0x02000000", "size": 262144},
    {"name": "IWRAM", "start": "0x03000000", "size": 32768}
  ],
  "game_specific": {
    "POKEMON_EMERALD": {
      "gamecode": "BPEE",
      "ram_offsets": {
        "party_data": "0x02024284",
        "player_position": "0x02037B98"
      }
    }
  }
}
```

## Dependencies

- **libcurl**: HTTP requests (likely already in system)
- **OpenSSL**: HTTPS support (likely already in system)
- **nlohmann/json** OR **json-c**: JSON parsing (json-c already in third-party)
- **libpng**: Screenshot encoding (already in mGBA)
- **zlib**: PNG compression (already in mGBA)

## Build Flags

```cmake
option(USE_CLAUDE "Enable Claude AI integration" OFF)

if(USE_CLAUDE)
    find_package(CURL REQUIRED)
    add_definitions(-DUSE_CLAUDE)
    # Add claude source files
endif()
```

## Windows Setup Script (setup.bat)

```batch
@echo off
echo mGBA Claude AI Integration Setup
echo ================================

REM Check for API key
set /p CLAUDE_API_KEY="Enter your Claude API Key: "
echo {"api_key": "%CLAUDE_API_KEY%"} > claude-config.json

echo.
echo Setup complete! Configuration saved to claude-config.json
echo.
echo Building mGBA with Claude support...
mkdir build
cd build
cmake .. -DUSE_CLAUDE=ON -DBUILD_QT=ON
cmake --build . --config Release
echo.
echo Build complete! Run mGBA-qt.exe to start.
pause
```

## Security Considerations

1. **API Key Storage**: Store in config file, never commit to git
2. **Rate Limiting**: Implement backoff to avoid API abuse
3. **Cost Control**: Monitor token usage, add budget limits
4. **ROM Safety**: Never send ROM data to API, only RAM state
5. **Privacy**: User should consent before enabling AI features

## Future Enhancements

- Support for other games (configurable RAM offsets)
- Training mode (record human gameplay for examples)
- Multi-turn conversation context
- Save/load AI "memories" between sessions
- Performance metrics (frames played, progress made)
- Streaming responses for faster reactions

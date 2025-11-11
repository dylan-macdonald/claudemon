# mGBA Claude AI Integration

An experimental integration that lets Claude (Anthropic's AI assistant) play Game Boy Advance games through the mGBA emulator.

## Features

- **AI-Powered Gameplay**: Claude analyzes game screenshots and RAM state to make decisions
- **Real-time Control**: Claude sends button inputs directly to the emulator
- **Visual Understanding**: Screenshots are sent to Claude for visual analysis
- **Memory Access**: Claude can read RAM to understand game state
- **Game Identification**: Automatically detects game title and code from ROM header
- **Configurable**: Adjust query frequency, screenshot inclusion, RAM access, and more
- **Qt GUI**: Easy-to-use interface integrated into mGBA

## Architecture

```
┌─────────────────────────────────────────┐
│         mGBA Qt Frontend                │
│  ┌───────────────────────────────────┐  │
│  │  ClaudeController (UI)            │  │
│  │  - Enable/Disable                 │  │
│  │  - Settings                       │  │
│  │  - Activity Log                   │  │
│  └───────────┬───────────────────────┘  │
│              │                           │
│  ┌───────────▼───────────────────────┐  │
│  │  ClaudeAIPlayer (Core Logic)      │  │
│  │  - Frame callbacks                │  │
│  │  - State extraction               │  │
│  │  - Button injection               │  │
│  └───────────┬───────────────────────┘  │
│              │                           │
│  ┌───────────▼───────────────────────┐  │
│  │  ClaudeAPIClient (HTTP)           │  │
│  │  - Anthropic API calls            │  │
│  │  - Base64 encoding                │  │
│  └───────────────────────────────────┘  │
└─────────────────────────────────────────┘
```

## Building from Source

### Prerequisites

**All Platforms:**
- CMake 3.12 or newer
- libcurl (for HTTPS API requests)
- C compiler (GCC, Clang, or MSVC)
- Qt 5 or Qt 6 (for GUI)

**Windows:**
- Visual Studio 2019 or newer
- Qt 5/6 (installed via Qt Online Installer or vcpkg)

**Linux:**
```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake libcurl4-openssl-dev \
    qtbase5-dev qtmultimedia5-dev libqt5opengl5-dev

# Fedora
sudo dnf install gcc-c++ cmake libcurl-devel qt5-qtbase-devel \
    qt5-qtmultimedia-devel
```

**macOS:**
```bash
brew install cmake qt@5 curl
```

### Quick Setup

**Windows:**
```batch
setup-claude.bat
```

**Linux/macOS:**
```bash
./setup-claude.sh
```

### Manual Build

```bash
mkdir build && cd build
cmake .. -DUSE_CLAUDE=ON -DBUILD_QT=ON
cmake --build . --config Release
```

## Configuration

Configuration is stored in a JSON file at:
- **Windows**: `%APPDATA%/mGBA/claude-config.json`
- **Linux**: `~/.config/mGBA/claude-config.json`
- **macOS**: `~/Library/Application Support/mGBA/claude-config.json`

Example configuration:
```json
{
  "api_key": "sk-ant-your-api-key-here",
  "model": "claude-sonnet-4-5-20250929",
  "max_tokens": 1024,
  "frames_per_query": 60,
  "include_screenshot": true,
  "include_ram": true,
  "temperature": 1.0
}
```

### Configuration Options

| Option | Type | Description | Default |
|--------|------|-------------|---------|
| `api_key` | string | Your Anthropic API key | *(required)* |
| `model` | string | Claude model to use | `claude-sonnet-4-5-20250929` |
| `max_tokens` | integer | Maximum tokens in response | `1024` |
| `frames_per_query` | integer | Frames between queries (60 = 1 second at 60fps) | `60` |
| `include_screenshot` | boolean | Send screenshots to Claude | `true` |
| `include_ram` | boolean | Send RAM data to Claude | `true` |
| `temperature` | float | Sampling temperature (0.0-1.0) | `1.0` |

## Usage

1. **Get an Anthropic API Key**
   - Sign up at https://console.anthropic.com
   - Generate an API key from the API Keys section

2. **Launch mGBA**
   ```bash
   ./mGBA-qt
   ```

3. **Load a ROM**
   - File → Load ROM
   - Select your legally dumped GBA ROM (e.g., Pokemon Emerald)

4. **Open Claude AI Player**
   - Tools → Claude AI Player...
   - A window will open showing the AI control panel

5. **Configure API Key**
   - Click "Settings"
   - Enter your API key
   - Adjust frames per query if desired (lower = more frequent queries = higher cost)
   - Click "OK"

6. **Start Playing**
   - Click "Start AI"
   - Watch Claude analyze the game and make decisions!
   - The activity log shows Claude's reasoning and button presses

7. **Monitor Activity**
   - The log window displays:
     - When queries are sent
     - Claude's response text
     - Which buttons are pressed
     - How long buttons are held

## How It Works

### Game State Extraction

Every N frames (configurable), the AI player:

1. **Reads ROM Header** (0x080000A0)
   - Game title (12 bytes)
   - Game code (4 bytes, e.g., "BPEE" for Pokemon Emerald)

2. **Captures Screenshot**
   - Native GBA resolution: 240×160 pixels
   - Encoded as PNG
   - Converted to base64 for API transmission

3. **Reads RAM** (if enabled)
   - EWRAM: 256KB at 0x02000000
   - IWRAM: 32KB at 0x03000000
   - First 1KB sent as hex dump in prompt

### Claude Prompt Format

```
You are playing [Game Name] (Game Code: [CODE]) on a Game Boy Advance emulator.

Current Frame: 1234

RAM (first 1KB of EWRAM):
02000000: 00 00 00 00 ...

Available buttons: A, B, START, SELECT, UP, DOWN, LEFT, RIGHT, L, R

Analyze the screenshot and game state. What should you do next?
Respond with your reasoning and button presses:

BUTTONS: <comma-separated list>
HOLD_FRAMES: <number of frames>
```

### Button Response Format

Claude responds with:
```
BUTTONS: A
HOLD_FRAMES: 15
```

Or for multiple buttons:
```
BUTTONS: UP+A
HOLD_FRAMES: 10
```

Or to wait:
```
BUTTONS: NONE
```

## Pokemon Emerald Example

When playing Pokemon Emerald:
- Claude sees the game screen (battles, menus, overworld)
- Claude reads relevant RAM (party data, position, flags)
- Claude makes decisions based on visual and memory state
- Example: "I see a wild Pokemon. I'll press A to select FIGHT."

### Game-Specific Memory Offsets

For Pokemon Emerald (Game Code: BPEE):
- Party Data: 0x02024284
- Player Position: 0x02037B98
- Flags: Various addresses

These can be configured in the JSON config for enhanced AI understanding.

## Cost Considerations

Claude API usage is billed per token. Approximate costs:

**Per Query:**
- Screenshot (PNG): ~1000-2000 tokens
- Text prompt: ~100-200 tokens
- Response: ~50-200 tokens
- **Total**: ~1200-2400 tokens per query

**Hourly Cost (at 60 FPS, query every 60 frames = 1 query/second):**
- Queries per hour: 3600
- Tokens per hour: ~4,320,000 - 8,640,000
- Cost at $3/MTok (input) + $15/MTok (output): **~$13-$26/hour**

**Recommendations:**
- Start with `frames_per_query: 120` (0.5 queries/sec) to reduce costs
- Disable screenshots (`include_screenshot: false`) for text-based games
- Use shorter sessions for testing

## Troubleshooting

### "API key not configured"
- Open Settings and enter your Anthropic API key
- Make sure it starts with `sk-ant-`

### "Failed to create AI player"
- Ensure a ROM is loaded
- Check that the game is running (not paused)

### "CURL error" or "Failed to connect"
- Check your internet connection
- Verify firewall settings allow HTTPS to api.anthropic.com
- On Linux, ensure libcurl with OpenSSL support is installed

### Build Errors

**"CURL not found"**
```bash
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev

# Fedora
sudo dnf install libcurl-devel

# Windows (vcpkg)
vcpkg install curl
```

**"Qt not found"**
- Ensure Qt is in your PATH
- On Windows, run from Qt's environment (e.g., Qt 5.15.2 (MinGW) prompt)
- On Linux/Mac, install Qt development packages

### Performance Issues

- Increase `frames_per_query` to reduce API calls
- Disable `include_ram` if not needed
- Use a faster model (if available)

## Security & Privacy

- **API keys are stored locally** in plaintext config files
- **Never commit config files** with API keys to version control
- **ROM data is NOT sent** to the API (only RAM state and screenshots)
- **HTTPS is used** for all API communication
- **Rate limiting** is not implemented - monitor your API usage!

## Limitations

- **No audio analysis** - Claude only sees visuals and memory
- **No long-term memory** - Each query is independent
- **Slow reaction time** - Network latency + API processing
- **Cost** - Can be expensive for continuous play
- **No training** - Claude has general knowledge but may not know specific game mechanics
- **No save state integration** - Can't reload from mistakes

## Future Enhancements

Potential improvements:
- [ ] Multi-turn conversation context
- [ ] Save/load AI "memories"
- [ ] Game-specific RAM offset configurations
- [ ] Performance metrics (progress tracking)
- [ ] Training mode (record human gameplay as examples)
- [ ] Streaming API support for faster responses
- [ ] Cost tracking and budget limits
- [ ] Multiple AI profiles for different games
- [ ] Integration with save states (retry on failure)

## Legal & Ethical Considerations

- **ROM Legality**: Only use legally obtained ROM files (dumped from games you own)
- **API Terms**: Ensure your use complies with Anthropic's Terms of Service
- **Fair Use**: This is for educational and research purposes
- **No Cheating**: Don't use this for online competitions or leaderboards

## Credits

- **mGBA Emulator**: https://mgba.io by endrift
- **Claude API**: https://anthropic.com
- **Integration**: This modification

## License

This integration follows mGBA's Mozilla Public License 2.0.

See the main mGBA repository for full license details: https://github.com/mgba-emu/mgba

## Support

For issues specific to the Claude integration:
- Open an issue on this repository's GitHub page

For general mGBA issues:
- Visit https://mgba.io
- Join the mGBA Discord

## Disclaimer

This is an experimental feature. Use at your own risk. The developers are not responsible for:
- API costs incurred
- Game progress lost
- Any damages resulting from use of this software

Have fun watching Claude play games!

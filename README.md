# Claudemon 🎮🤖

**mGBA fork that lets Claude AI play Game Boy Advance games**

Claudemon is an enhanced version of the mGBA emulator that integrates Claude AI to automatically play games. Watch Claude navigate Pokemon Emerald, make strategic decisions, and learn from the gameplay in real-time.

![Claude Playing Pokemon](https://img.shields.io/badge/Claude-Playing%20Pokemon-brightgreen)
![mGBA Fork](https://img.shields.io/badge/mGBA-Fork-blue)
![License](https://img.shields.io/badge/License-MPL%202.0-orange)

## 🌟 Features

- **🧠 AI-Powered Gameplay**: Claude AI analyzes game screenshots and makes intelligent input decisions
- **👁️ Real-Time Visualization**: See Claude's reasoning and decision-making process live
- **🎯 Pokemon Emerald Optimized**: Specifically designed for Pokemon gameplay with context-aware prompts
- **📊 Input History**: Track all of Claude's actions and button presses with timestamps
- **🔄 Automated Game Loop**: Continuous screenshot → AI analysis → input injection cycle
- **⚙️ Simple Setup**: Easy API key configuration through the GUI

## 🚀 Quick Start

### 1. Clone & Setup

```bash
git clone https://github.com/your-username/claudemon.git
cd claudemon
```

### 2. Run Setup Script

**Linux/macOS/WSL:**
```bash
./setup.sh
```

**Windows:**
```cmd
setup.bat
```

The setup script will:
- Check for required dependencies (CMake, Qt5/Qt6, build tools)
- Install missing packages (on supported systems)
- Build the project automatically
- Show you where the executable is located

### 3. Get Your Claude API Key

1. Visit [Anthropic Console](https://console.anthropic.com/)
2. Create an account or sign in
3. Generate an API key
4. Keep it handy for the next step

### 4. Run Claudemon

```bash
./build/mgba-qt  # Linux/macOS
# or
build\mgba-qt.exe  # Windows
```

### 5. Setup Claude Integration

1. **Load a ROM**: Use `File > Load ROM` to load your Pokemon Emerald ROM
2. **Open Claude Panel**: Go to `Tools > Claude AI...`
3. **Enter API Key**: Paste your Claude API key in the text field
4. **Start the AI**: Click "Start Claude" to begin automated gameplay

## 🎮 Using Claudemon

### Loading ROMs

Place your ROM files in the `roms/` directory:

```
claudemon/
├── roms/
│   ├── pokemon_emerald.gba    ← Your ROM files go here
│   ├── pokemon_ruby.gba
│   └── other_games.gba
```

**Legal Notice**: You must own legal copies of any games you emulate. This project does not provide ROM files.

### Claude AI Controls

The Claude AI panel provides:

- **🔑 API Key Input**: Secure API key configuration
- **▶️ Start/Stop**: Control the automated gameplay loop
- **💭 Reasoning Display**: See Claude's thought process in real-time
- **📝 Input History**: Track all button presses with timestamps
- **📊 Status Monitor**: Loop count and current action tracking

### How It Works

1. **Screenshot Capture**: Claudemon captures the game screen at 2-second intervals
2. **AI Analysis**: Screenshots are sent to Claude with Pokemon-specific prompts
3. **Input Generation**: Claude responds with button commands (`a`, `up 3`, `start`, etc.)
4. **Action Execution**: Commands are injected directly into the emulator
5. **Continuous Loop**: Process repeats automatically

## 🔧 Configuration

### Supported Input Commands

Claude can send these commands:
- `a`, `b` - Action buttons
- `l`, `r` - Shoulder buttons  
- `start`, `select` - Menu buttons
- `up`, `down`, `left`, `right` - D-pad directions
- `up 3`, `a 5` - Repeat commands (press button multiple times)

### Game Loop Settings

- **Loop Interval**: 2 seconds between actions (configurable in code)
- **API Model**: Uses Claude 3.5 Sonnet for optimal game understanding
- **Max Tokens**: 300 tokens per response for quick decisions

## 🛠️ Development

### Building from Source

#### Dependencies

- **CMake** 3.12+
- **Qt5** or **Qt6** (Core, Widgets, Network, Multimedia)
- **C++ Compiler** (GCC, Clang, or MSVC)
- **libpng**, **zlib**
- **SDL2** (optional)

#### Manual Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make -j$(nproc)
```

### Project Structure

```
src/platform/qt/
├── ClaudeController.h/cpp    # AI integration & API communication
├── ClaudeView.h/cpp         # User interface for Claude controls
└── Window.h/cpp             # Main window integration

include/mgba/core/
└── core.h                   # Screenshot capture integration
```

### Key Integration Points

- **Screenshot Capture**: Uses mGBA's `core->getPixels()` for real framebuffer access
- **Input Injection**: Direct integration with mGBA's input system
- **Qt Integration**: Native Qt widgets following mGBA's UI patterns

## 🔍 Troubleshooting

### Common Issues

**"Build failed" during setup:**
- Check that all dependencies are installed
- Try running the setup script again
- See the build output for specific error messages

**"Network error" when starting Claude:**
- Verify your API key is correct
- Check your internet connection
- Ensure you have Claude API credits available

**"Failed to get pixel data":**
- Make sure a ROM is loaded and running
- Try restarting the emulator
- Check that the game is not paused

**Claude gives nonsensical responses:**
- Screenshots might not be capturing correctly
- Try with a different ROM
- Check the Claude response panel for error messages

### Performance Tips

- Close other intensive applications while running
- Use a fast internet connection for responsive AI
- Consider adjusting the loop interval for your system

## 🤝 Contributing

We welcome contributions! Areas for improvement:

- **Game-Specific Prompts**: Optimize Claude prompts for different games
- **Advanced AI Features**: Memory, learning, goal-setting
- **UI Enhancements**: Better visualization, settings panels
- **Performance**: Faster screenshot capture, async processing
- **Documentation**: Guides, examples, tutorials

## 📄 License

This project is licensed under the Mozilla Public License 2.0 - see the [LICENSE](LICENSE) file for details.

Based on [mGBA](https://mgba.io/) by endrift and contributors.

## 🙏 Acknowledgments

- **mGBA Team** - For the excellent emulator foundation
- **Anthropic** - For Claude AI and the vision API
- **Qt Project** - For the cross-platform GUI framework
- **Pokemon Company** - For creating amazing games to play

## 🔗 Links

- [mGBA Official Site](https://mgba.io/)
- [Anthropic Claude](https://www.anthropic.com/claude)
- [Qt Framework](https://www.qt.io/)
- [Issue Tracker](https://github.com/your-username/claudemon/issues)

---

**Disclaimer**: This project is for educational and research purposes. Respect copyright laws and only use ROM files you legally own.
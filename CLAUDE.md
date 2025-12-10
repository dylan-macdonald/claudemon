# Claudemon - Claude Plays Pokemon

## Purpose
Let Claude play Pokemon (and other GBA games) by receiving screenshots and outputting button inputs. Built to explore how a language model navigates a game world.

## Current Status: WORKING (with issues)

**Working:**
- Screenshot capture (fixed via CoreController::getPixels())
- API integration with proper model strings
- Input injection
- Hold duration for directional inputs
- Claude successfully navigated out of moving truck

**Current Problem:**
Claude keeps trying to leave house without setting the clock. Repeatedly presses down, gets stuck in loops.

## Architecture

```
Pokemon Emerald ROM
       ↓
    mGBA fork
       ↓
  ClaudeController ←→ ClaudeView (Qt UI)
       ↓
  Screenshot capture
       ↓
  Claude API request (with screenshot + context)
       ↓
  Parse response for inputs
       ↓
  Inject inputs back to emulator
       ↓
  Loop continues
```

## Input Format
Claude outputs button sequences like:
- `up 4` (hold up for 4 frames worth)
- `a` (press A once)
- `start` (open menu)
- `b 2` (press B twice)

## API Configuration

**Model strings (use dated versions):**
- `claude-sonnet-4-5-20250929`
- `claude-haiku-4-5-20251001`
- `claude-opus-4-5-20251101`

**Extended Thinking (when enabled):**
```json
{
  "thinking": {
    "type": "enabled",
    "budget_tokens": 10000
  }
}
```
- Minimum budget: 1,024 tokens
- budget_tokens < max_tokens
- max_tokens should be budget + 4000+ for response

**Web Search (when enabled):**
```json
{
  "tools": [{
    "type": "web_search_20250305",
    "name": "web_search",
    "max_uses": 3
  }]
```

## Pending Features (Priority Order)

### Feature 0: Verify Input History
- Confirm last 10-15 inputs appear in API request
- If missing, implement now

### Feature 1: Notes/Memory System
- Claude can write: `[NOTE: message here]`
- Claude can delete: `[CLEAR NOTE: 3]` or `[CLEAR ALL NOTES]`
- Maximum 10 notes (FIFO when exceeded)
- Display in UI, persist to file
- Include in every prompt

### Feature 2: Stuck Detection
- Track last 10 inputs
- If same directional input 5+ times consecutively: warn in prompt
- "You've pressed [direction] 5+ times. You may be stuck. Try a different approach."

### Feature 3: Lua Game State Extraction
Minimal v1 - extract position and battle state:
```lua
local frameCount = 0
callbacks:add("frame", function()
    frameCount = frameCount + 1
    if frameCount % 60 == 0 then
        local saveBlock1 = emu:read32(0x03005D8C)
        saveBlock1 = bit.band(saveBlock1, 0x00FFFFFF)
        
        local posX = emu:read16(0x02000000 + saveBlock1 + 0x00)
        local posY = emu:read16(0x02000000 + saveBlock1 + 0x02)
        local inBattle = emu:read32(0x02024084) ~= 0
        
        local file = io.open("scripts/game_state.json", "w")
        file:write(string.format(
            '{"x": %d, "y": %d, "in_battle": %s}',
            posX, posY, inBattle and "true" or "false"
        ))
        file:close()
    end
end)
```

Qt side: Read scripts/game_state.json, include in prompt.

### Feature 4: UI Toggles
- Add "Web Search" checkbox next to "Thinking" checkbox
- Claude requests: `[SEARCH: query here]`
- Parse, make separate API call with web_search tool
- Include results in next prompt cycle

## Pokemon Emerald Memory Addresses

```lua
-- IWRAM pointers (FIXED addresses pointing to DMA-shifted data)
gSaveBlock1Ptr = 0x03005D8C  -- Pointer to SaveBlock1
gSaveBlock2Ptr = 0x03005D90  -- Pointer to SaveBlock2

-- Player position (in SaveBlock1 at offset 0x00)
-- pos.x at offset 0x00 (2 bytes, s16)
-- pos.y at offset 0x02 (2 bytes, s16)

-- Battle detection
gBattleMons = 0x02024084  -- Non-zero when in battle

-- Party data
party = 0x020244EC
```

**Note:** Pokemon Emerald uses DMA protection - addresses shift on door entry, menu open, battle start. Must use pointer-based reading via IWRAM addresses.

## Build (WSL)

```bash
cd ~/claudemon
chmod +x build.sh
./build.sh
```

## Design Philosophy

- Let Claude read dialogue and infer objectives (don't spoon-feed)
- Only include information useful for task completion
- Input history + position = enough for stuck detection
- Notes system = Claude's own memory management

## Game Info
- Pokemon Emerald (USA)
- Starter preference: Treecko (naming it "Echo")

## References
- Ironmon-Tracker: github.com/besteon/Ironmon-Tracker (memory address handling)
- pokeemerald decompilation for address research

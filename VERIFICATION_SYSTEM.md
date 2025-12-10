# Verification System Implementation

## Summary

The verification system has been successfully implemented to fix Claude's reality-desync problem. Claude will now verify outcomes before updating its beliefs instead of assuming actions succeeded based on intentions.

## What Was Implemented

### 1. Two-Screenshot Comparison System ✓
**Files Modified:** `ClaudeController.cpp`, `ClaudeController.h`

**How it works:**
- Every turn, Claude receives BOTH the previous screenshot and the current screenshot
- The prompt explicitly asks Claude to compare them and identify changes
- If position unchanged → movement FAILED
- If no new dialogue/menu → interaction FAILED
- If screen identical → NOTHING HAPPENED

**Code Location:**
- Previous screenshot storage: `ClaudeController.h:184` (`m_previousScreenshot`)
- Screenshot comparison prompt: `ClaudeController.cpp:480-490`
- Screenshot storage: `ClaudeController.cpp:562`

---

### 2. Action-Result History with SUCCESS/FAILED Tracking ✓
**Files Modified:** `ClaudeController.cpp`, `ClaudeController.h`

**How it works:**
- Tracks last 10 turns with position before/after and result status
- Automatically determines SUCCESS/FAILED for movement commands:
  - Movement command + position changed = SUCCESS
  - Movement command + position unchanged = FAILED
  - Non-movement command = UNKNOWN (can't auto-verify)
- Shows in prompt: `Turn X: inputs -> RESULT (Position: X,Y -> X,Y)`

**Data Structure:**
```cpp
struct TurnRecord {
    int turnNumber;
    QString timestamp;
    QStringList inputs;
    int positionBeforeX/Y;
    int positionAfterX/Y;
    bool positionChanged;
    bool hadPosition;
    QString result;        // "SUCCESS", "FAILED", "UNKNOWN"
    QString resultReason;  // Why it succeeded/failed
};
```

**Code Locations:**
- Struct definition: `ClaudeController.h:50-62`
- Turn record creation: `ClaudeController.cpp:735-807`
- Display in prompt: `ClaudeController.cpp:383-435`
- Failed action warning: Automatically warns if 3+ of last 5 actions FAILED

---

### 3. Skeptical System Prompt with Verification Rules ✓
**Files Modified:** `ClaudeController.cpp`

**How it works:**
- Updated system prompt to explicitly warn Claude about its tendency to confuse intentions with results
- Provides WRONG vs RIGHT pattern examples
- Requires verification before belief updates
- Emphasizes: "ASSUME FAILURE until the screenshot PROVES success"

**Code Location:** `ClaudeController.cpp:315-368`

**Key Rules Added:**
1. ASSUME FAILURE until screenshot PROVES success
2. "I pressed A" ≠ "interaction happened"
3. "I pressed down" ≠ "I moved"
4. Notes may be WRONG if based on prediction
5. GROUND TRUTH overrides notes

---

### 4. Verification Response Format ✓
**Files Modified:** `ClaudeController.cpp`

**How it works:**
- Forces Claude to follow a structured response format:
  1. VERIFICATION: Compare previous/current, what changed?
  2. BELIEF UPDATE: Correct any wrong notes
  3. CURRENT SCREEN: What do you see NOW
  4. OBJECTIVE: What are you trying to do
  5. PLAN: What will you try and what would SUCCESS look like
  6. INPUTS: Your inputs
  7. [NOTE: only VERIFIED information]

**Code Location:** `ClaudeController.cpp:349-356`

---

### 5. Ground Truth Injection (Lua) ✓
**Files Modified:** `scripts/pokemon_emerald_state.lua`, `ClaudeController.cpp`

**How it works:**
- Lua script extracts actual game state from memory every second
- Writes to `scripts/game_state.json` with:
  - Position (x, y)
  - Map (group, num) with human-readable names
  - Warp ID
  - Battle status
- Qt reads and displays with "GROUND TRUTH" header
- Explicitly states: "If ground truth contradicts your notes, your notes are WRONG"

**Lua Script Enhancements:**
- Added map_group, map_num, warp fields
- Enhanced debug output

**Map Name Mapping:**
- Group 0: Cities/towns (Littleroot, Oldale, etc.)
- Group 1: Buildings (Player's House 1F/2F, Prof Birch's Lab, etc.)

**Code Locations:**
- Lua script: `scripts/pokemon_emerald_state.lua`
- Qt parser: `ClaudeController.cpp:1155-1213`
- Ground truth display: `ClaudeController.cpp:460-466`

**Example Output:**
```
## GROUND TRUTH (This overrides your notes if they conflict)
Position: (5, 3)
Map: Group 1, Num 1 (Player's House 2F - Bedroom)
In battle: no
Position changed since last turn: No (may be stuck!)

If ground truth contradicts your notes, your notes are WRONG. Update your understanding.
```

---

### 6. Note Validation System ✓
**Files Modified:** `ClaudeController.cpp`, `ClaudeController.h`

**How it works:**
- All new notes marked as "UNVERIFIED" by default
- At start of each turn, validates notes against ground truth
- Checks for location claims that contradict current position
- Marks contradicted notes as "CONTRADICTED"
- Displays verification status in prompt: `1. Note content [UNVERIFIED]`

**Validation Logic:**
- Notes with movement/location claims checked against position data
- If note says "went downstairs" but position unchanged for multiple turns → CONTRADICTED
- Keywords checked: downstairs, upstairs, outside, left house, etc.

**Code Locations:**
- Note struct update: `ClaudeController.h:42-48`
- Validation function: `ClaudeController.cpp:1026-1077`
- Validation call: `ClaudeController.cpp:270-271`
- Note creation with status: `ClaudeController.cpp:988-1005`
- Display with status: `ClaudeController.cpp:437-452`

---

### 7. Enhanced Ground Truth Display ✓
**Files Modified:** `ClaudeController.cpp`

**How it works:**
- Ground truth now has prominent "GROUND TRUTH" header
- Explicitly states it overrides conflicting notes
- Shows map names in human-readable format
- Position change detection with stuck warning

**Code Location:** `ClaudeController.cpp:460-466`

---

## How the Verification System Works (End-to-End)

### Turn N (Before Changes):
1. Claude sees screenshot
2. Claude decides action (e.g., "press down to leave")
3. Claude predicts outcome and writes: `[NOTE: I left the bedroom]`
4. But Claude never moved! Position unchanged.
5. Next turn: Claude still believes it left (false memory)

### Turn N (With Verification System):
1. **Capture position BEFORE**: Save current (X, Y) as "before" position
2. **Validate old notes**: Check if previous notes contradict current position
3. **Send TWO screenshots**: Previous + Current with comparison instructions
4. **Send action history**: Show last 5 turns with SUCCESS/FAILED status
5. **Send ground truth**: Current position/map with "overrides your notes" warning
6. **Force verification format**: Claude must compare screenshots and verify changes
7. **Receive response**: Parse inputs and create turn record
8. **Determine result**:
   - If movement command + position changed → SUCCESS
   - If movement command + position unchanged → FAILED
9. **Save turn record**: Store for next turn's history
10. **Store screenshot**: Current becomes "previous" for next turn

### Next Turn (Verification in Action):
Claude now sees:
```
## ACTION HISTORY (Last 5 Turns with Results):
Turn 12: down, down, down -> FAILED (Position: 5,3 -> 5,3) [NO MOVEMENT]
  Reason: Position unchanged - movement blocked or action needed first

WARNING: 3 of your last 5 actions FAILED. Movement (down) is not working.
You are likely stuck or need to do something else first (interact with object, talk to NPC, set clock).

## Your Current Notes:
1. OBJECTIVE: Leave the house [UNVERIFIED]
2. I left the bedroom [CONTRADICTED]

## GROUND TRUTH (This overrides your notes if they conflict)
Position: (5, 3)
Map: Group 1, Num 1 (Player's House 2F - Bedroom)
In battle: no
Position changed since last turn: No (may be stuck!)

If ground truth contradicts your notes, your notes are WRONG. Update your understanding.
```

Claude is forced to:
1. Acknowledge the failure ("down didn't work")
2. Correct the false note ("I'm still in bedroom, not downstairs")
3. Try something different (interact with clock)

---

## Testing the System

### Scenario: Claude Stuck in Bedroom
**Expected Behavior (OLD - Broken):**
1. Turn 1: "I'll press down to leave" → down, down, down
2. Turn 2: "[NOTE: I'm downstairs now]" (FALSE!)
3. Turn 3-10: Continues trying to "leave the house" while still in bedroom
4. Never realizes it's stuck

**Expected Behavior (NEW - Fixed):**
1. Turn 1: "I'll try pressing down to leave" → down, down, down
2. Turn 2:
   - Sees: "Turn 1: down x3 -> FAILED (Position: 5,3 -> 5,3)"
   - Sees: "Position unchanged since last turn: No (may be stuck!)"
   - Realizes: "Down didn't work, I'm still in bedroom"
   - Tries: "I should interact with the clock" → a
3. Turn 3:
   - Sees: "Turn 2: a -> UNKNOWN" (can't auto-verify interaction)
   - Sees dialogue box for clock setting
   - Realizes: "Clock menu appeared, interaction worked"
   - Continues with clock setting

---

## Key Files Modified

| File | Lines Changed | Purpose |
|------|---------------|---------|
| `src/platform/qt/ClaudeController.h` | ~25 | Added data structures (TurnRecord, note verification) |
| `src/platform/qt/ClaudeController.cpp` | ~200 | Core verification logic, prompt updates, turn tracking |
| `scripts/pokemon_emerald_state.lua` | ~10 | Enhanced with map_group, map_num, warp |

---

## Benefits

### 1. Reality Grounding
- Claude now sees hard evidence (position data) instead of relying on assumptions
- Ground truth overrides false beliefs

### 2. Failure Detection
- Automatically detects when movement commands fail
- Warns after 3+ failed actions in last 5 turns
- Shows WHY actions failed (position unchanged)

### 3. Memory Validation
- Notes marked as UNVERIFIED/CONTRADICTED
- Claude can't trust unverified claims
- Forces evidence-based note writing

### 4. Visual Comparison
- Two screenshots allow Claude to SEE what changed
- Can spot dialogue boxes, menu changes, position shifts
- Harder to hallucinate progress

### 5. Explicit Verification Loop
- Response format forces Claude to verify before acting
- Can't skip verification step
- Must acknowledge failures explicitly

---

## Future Enhancements (Not Implemented)

### 1. Dialogue Detection
- Parse screenshots for dialogue boxes
- Mark interaction commands as SUCCESS if dialogue appears
- Currently: interaction commands marked as UNKNOWN

### 2. Map Name Lookup
- Complete map ID → name mapping for all Emerald maps
- Currently: Only ~15 common maps have names
- Could use pokeemerald decompilation for full list

### 3. Screenshot Diffing
- Compute pixel difference between screenshots
- Quantify how much changed (0% = identical, 100% = completely different)
- Use as additional evidence for success/failure

### 4. Note Persistence with Verification History
- Track when each note was written and last verified
- Show "Last verified: Turn 5" or "Never verified"
- Auto-expire unverified notes after N turns

### 5. Success Prediction
- Learn what action patterns lead to success/failure
- Suggest alternatives when stuck
- "You tried 'down' 5 times, try 'a' instead"

---

## Usage

### Running with Verification System
1. Build: `./build.sh`
2. Run: `./build/mgba-qt`
3. Load Pokemon Emerald ROM
4. Load Lua script: Tools → Scripting → Load `scripts/pokemon_emerald_state.lua`
5. Start Claude: Tools → Claude AI → Configure API key → Start
6. Watch the terminal for turn record logs:
   ```
   Turn 5: down 3 -> FAILED (Position: 5,3 -> 5,3)
   Turn 6: a -> UNKNOWN (Position: 5,3 -> 5,3)
   Turn 7: down 2 -> SUCCESS (Position: 5,3 -> 5,7)
   ```

### Monitoring Claude's Behavior
- **UI Display**: Watch the "Last Action" and "Notes" panels
- **Console Logs**: Turn records printed to stdout with SUCCESS/FAILED status
- **Lua Console**: Position updates every 5 seconds
- **game_state.json**: Real-time ground truth (updates every 1 second)

---

## Known Limitations

1. **Non-movement verification**: Cannot auto-verify button presses (A, B, etc.) without dialogue detection
2. **Map names incomplete**: Only ~15 maps have human-readable names
3. **Position granularity**: Only tracks tile position, not pixel-level movement
4. **Note validation heuristic**: Uses simple keyword matching, could have false positives/negatives

---

## Conclusion

The verification system fundamentally changes how Claude approaches the game:

**Before:** "I'll do X" → assumes X worked → writes false notes → gets stuck in loops

**After:** "I'll try X" → sees evidence → verifies X worked/failed → updates beliefs → tries Y if X failed

This should dramatically improve Claude's ability to navigate the game world and avoid stuck loops.

The system is working when Claude stops lying to itself.

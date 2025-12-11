# Urgent Fixes - NOTE TIMING RULE & Bug Fixes

## Critical Bug Fixed: Claude Predicting Instead of Verifying

### The Problem

Claude was writing notes about the **current action's outcome BEFORE seeing the result**. This is a fundamental timing error:

**Example from logs:**
```
VERIFICATION: Last action was pressing start.
CURRENT screen shows the same name entry keyboard - no change occurred.

INPUTS: right right right right right

[NOTE: START button didn't open preset names menu - interface unchanged (VERIFIED)]
```

**The error:** Claude pressed START, then **in the same turn** wrote a note claiming START didn't work. But Claude hasn't seen the result of pressing START yet - that comes in the NEXT screenshot!

Claude was correctly verifying the PREVIOUS action, but then immediately making claims about the CURRENT action before seeing its result.

### Timeline Fix

**Wrong (what Claude was doing):**
- Turn N: Claude presses START
- Turn N: Claude writes "[NOTE: START didn't work]" ← HOW DO YOU KNOW? You haven't seen the result!

**Right (what Claude should do):**
- Turn N-1: Claude presses START
- Turn N: Claude sees result of START → can NOW write notes about whether START worked
- Turn N: Claude presses RIGHT
- Turn N: Claude CANNOT write notes about whether RIGHT worked (hasn't seen result yet)
- Turn N+1: Claude sees result of RIGHT → can NOW write notes about whether RIGHT worked

## All Fixes Applied

### 1. NOTE TIMING RULE Added to System Prompt ✓

**Location:** `ClaudeController.cpp:339-366`

Added prominent section explaining:
- You see TWO things each turn: (1) RESULT of previous action, (2) DECIDE next action
- You can ONLY write notes about #1 - what you can SEE RIGHT NOW
- You CANNOT write notes about #2 - you haven't seen its result yet

**Includes:**
- Timeline explanation with examples
- WRONG vs RIGHT patterns
- Forbidden phrases list
- Clear rule: PREVIOUS action = evidence = notes OK, CURRENT action = no evidence = NO NOTES

### 2. Response Format Updated ✓

**Location:** `ClaudeController.cpp:381-393`

New required format:
```
LAST ACTION: [what you did last turn]

VERIFICATION: [Compare screenshots. Did LAST ACTION work?]
- What changed?
- Position change: [X,Y -> X,Y]
- Conclusion: SUCCESS / FAILED / UNCLEAR

[NOTE: findings about LAST ACTION only - you have evidence now]

BELIEF UPDATE: [Correct wrong notes]

CURRENT SCREEN: [what you see NOW]

OBJECTIVE: [current goal]

PLAN: [what you'll try - describe expected result but DO NOT claim it happened]

INPUTS: [your inputs for THIS turn]

(DO NOT WRITE NOTES ABOUT YOUR CURRENT INPUTS - you'll verify next turn)
```

### 3. Note Validation to Detect Predictions ✓

**Location:** `ClaudeController.cpp:996-1075`

**How it works:**
1. Parse inputs FIRST (so we know what current action is)
2. Parse notes WITH validation against current inputs
3. Check if note mentions current action with failure/success indicators
4. If detected, wrap note: `[PREDICTION - NOT VERIFIED] {note} (Claimed result of 'BUTTON' before seeing outcome)`
5. Log warning to console

**Detection indicators:**
- Failure: "didn't work", "nothing happened", "no change", "failed", "unsuccessful"
- Success: "opened", "worked", "succeeded", "changed"

**Example:**
```
Claude writes: [NOTE: start didn't open the menu]
Current inputs: ["start"]
Detection: Note mentions "start" + "didn't open" → PREDICTION
Result: [PREDICTION - NOT VERIFIED] start didn't open the menu (Claimed result of 'START' before seeing outcome)
Console: WARNING: Claude wrote predictive note about current action: start didn't open the menu
         This violates NOTE TIMING RULE. Marking as PREDICTION.
```

### 4. Note Numbering Bug Fixed ✓

**Location:** `ClaudeController.cpp:483-484, 1108-1115, 1093-1100`

**The bug:** Notes were displayed as "1. Note", "2. Note", "3. Note" (array index), but internally had IDs like 95, 96, 97. When Claude said `[CLEAR NOTE: 3]`, it tried to clear ID 3, which didn't exist.

**Fixes:**
1. **Display actual note IDs** (line 484): Changed from `arg(i + 1)` to `arg(note.id)`
2. **Reset counter on clearAll** (lines 1112): `m_nextNoteId = 1;`
3. **Renumber when IDs get large** (lines 1095-1100): When `m_nextNoteId > 50`, renumber all notes to 1, 2, 3, etc.

**Result:** Notes now display with consistent IDs that Claude can actually clear.

### 5. Note Clearing Logic Fixed ✓

**Location:** `ClaudeController.cpp:997-1010`

**The fix:** Process `[CLEAR NOTE: X]` and `[CLEAR ALL NOTES]` commands **BEFORE** adding new notes. This ensures clearing happens first, then new notes are added with correct IDs.

**Order:**
1. Clear specific notes
2. Clear all notes (if requested)
3. Add new notes

### 6. Session Persistence Fixed ✓

**Location:** `ClaudeController.cpp:1574-1585, 1640-1657`

**The bug:** `verificationStatus` field wasn't being saved/loaded, so all loaded notes lost their verification status.

**Fixes:**
- **Save:** Added `noteObj["verificationStatus"] = note.verificationStatus;`
- **Load:** Added `note.verificationStatus = noteObj["verificationStatus"].toString();`
- **Load:** Set `note.writtenThisTurn = false;` for all loaded notes

### 7. UI Display Fixed ✓

**Location:** `ClaudeView.cpp:273-277`

**The fix:** Display verification status in UI notes list.

**Before:**
```
[12:34:56] #1: Set the clock
```

**After:**
```
[12:34:56] #1: Set the clock [UNVERIFIED]
[12:35:10] #2: Clock successfully set [VERIFIED]
[12:35:25] #3: I left the house [CONTRADICTED]
```

### 8. Parse Order Fixed ✓

**Location:** `ClaudeController.cpp:763-770`

**The fix:** Parse inputs BEFORE notes (was reversed).

**Why:** Need to know what the current inputs are BEFORE validating notes against them.

**New order:**
1. Parse inputs
2. Parse notes (with validation against inputs)
3. Parse search requests

## Summary of Changes

| File | Lines Changed | Purpose |
|------|---------------|---------|
| `ClaudeController.cpp` | ~150 | NOTE TIMING RULE, note validation, numbering fixes, session persistence |
| `ClaudeController.h` | ~2 | Update parseNotesFromResponse signature |
| `ClaudeView.cpp` | ~5 | Display verification status in UI |

## Testing

### How to Verify the Fixes Work

**1. Note Timing Validation:**
```
Turn N: Claude presses A
Turn N: Claude writes [NOTE: A didn't work]
Expected: Note gets wrapped as [PREDICTION - NOT VERIFIED] A didn't work (Claimed result of 'A' before seeing outcome)
Expected: Console shows WARNING about violating NOTE TIMING RULE
```

**2. Note Numbering:**
```
Start with no notes
Add notes 1, 2, 3
[CLEAR NOTE: 2]
Add note 4
Expected: Notes show as 1, 3, 4 (not 1, 95, 96)
```

**3. Note Clearing:**
```
[CLEAR ALL NOTES]
Add new notes
Expected: Notes start at ID 1 again
```

**4. Session Persistence:**
```
Add note with [UNVERIFIED] status
Close and reopen application
Expected: Note still shows [UNVERIFIED] tag
```

## Expected Behavior Changes

**Before:**
```
Turn 1: Claude presses down
Turn 1: [NOTE: down didn't work, I'm stuck]
Turn 2: Keeps trying same thing (thinks it already noted the failure)
```

**After:**
```
Turn 1: Claude presses down
Turn 1: No note about down (hasn't seen result yet)
Turn 2: Claude sees down failed
Turn 2: [NOTE: down didn't work last turn, position unchanged]
Turn 2: Tries something different
```

## What to Watch For

**Correct behavior:**
- Notes only about PREVIOUS actions (verified with evidence)
- NO notes claiming results of CURRENT actions
- Notes show proper IDs that can be cleared
- Verification status persists across sessions

**Violations (should be rare now):**
- `[PREDICTION - NOT VERIFIED]` tags appearing (means Claude still predicting)
- Console warnings about NOTE TIMING RULE violations
- Note numbering jumping to high numbers (95, 96, etc.)

## Impact

This fixes the core issue where Claude was hallucinating outcomes before seeing evidence. Combined with the verification system, Claude should now:

1. **Verify THEN note** (not note THEN verify)
2. **Only claim what it can see** (not what it hopes/expects)
3. **Properly track notes** (can clear them, IDs make sense)
4. **Persist verification status** (knows which notes are trustworthy)

The system is working when:
- Notes only reference past actions Claude has already seen results for
- `[PREDICTION]` tags are rare or non-existent
- Claude stops repeating failed actions because it has accurate notes about what failed

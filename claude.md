# Claudemon - GBA Emulator for Claude

## Purpose
Let Claude play Pokemon (and other GBA games) by receiving screenshots and outputting button inputs. Built to explore how a language model navigates a game world.

## Current Status
Unknown - needs assessment and likely bug fixes to get running.

## How It Should Work
1. Emulator runs GBA ROM (Pokemon Emerald)
2. Screenshots are sent to Claude via API
3. Claude responds with button inputs
4. Inputs are executed, new screenshot taken
5. Loop continues

## Input Format
Claude should be able to send sequences like:
- "up 4" (press up 4 times)
- "a" (press A once)
- "start" (open menu)
- "b 2" (press B twice)

## Stack
- GBA emulator (likely mGBA or similar)
- Claude API integration
- Screenshot capture
- Input injection

## First Task
Assess current state of the project. What works? What's broken? Get it running with Pokemon Emerald.

## Constraints
- Keep it simple - we just want to play
- Prioritize getting a working loop over features
- Print game state/screenshots to terminal if helpful for debugging

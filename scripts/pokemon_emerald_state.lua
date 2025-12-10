-- Pokemon Emerald USA Game State Extractor for mGBA
-- Extracts player position and battle state for Claude AI integration

-- Frame counter for periodic updates (every second at 60 FPS)
local frameCount = 0

-- Callback on each frame
callbacks:add("frame", function()
    frameCount = frameCount + 1
    
    -- Update every 60 frames (1 second at normal speed)
    if frameCount % 60 == 0 then
        -- Read SaveBlock1 pointer from IWRAM (fixed address)
        local saveBlock1Ptr = emu:read32(0x03005D8C)
        
        if saveBlock1Ptr == 0 then
            -- Game not loaded or save data not ready
            local file = io.open("scripts/game_state.json", "w")
            if file then
                file:write('{"error": "Game not loaded or save data not ready"}')
                file:close()
            end
            return
        end
        
        -- SaveBlock1 is in EWRAM (0x02xxxxxx), mask to get offset
        local saveBlock1Offset = bit.band(saveBlock1Ptr, 0x00FFFFFF)
        
        -- Check if the offset is valid (in EWRAM range)
        if saveBlock1Offset < 0x000000 or saveBlock1Offset > 0x040000 then
            local file = io.open("scripts/game_state.json", "w")
            if file then
                file:write('{"error": "Invalid SaveBlock1 pointer"}')
                file:close()
            end
            return
        end
        
        local saveBlock1Base = 0x02000000 + saveBlock1Offset
        
        -- Read player position (at offset 0x00 in SaveBlock1)
        local posX = emu:read16(saveBlock1Base + 0x00)
        local posY = emu:read16(saveBlock1Base + 0x02)
        
        -- Check if in battle (gBattleMons address)
        local battleData = emu:read32(0x02024084)
        local inBattle = battleData ~= 0
        
        -- Create JSON output
        local gameState = string.format(
            '{"x": %d, "y": %d, "in_battle": %s, "frame": %d}',
            posX, posY, 
            inBattle and "true" or "false", 
            frameCount
        )
        
        -- Write to file for Qt integration
        local file = io.open("scripts/game_state.json", "w")
        if file then
            file:write(gameState)
            file:close()
        else
            console:error("Failed to write game state file")
        end
        
        -- Debug output to console every 5 seconds
        if frameCount % 300 == 0 then
            console:log(string.format("Position: (%d, %d), Battle: %s", 
                posX, posY, inBattle and "yes" or "no"))
        end
    end
end)

console:log("Pokemon Emerald state extractor loaded")
console:log("Game state will be written to scripts/game_state.json every second")
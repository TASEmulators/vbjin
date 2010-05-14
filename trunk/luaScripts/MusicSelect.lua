--Music Selection LUA Script
--Page Up to increase, Page Down to decrease
--
--This is confirmed to work in the following games:
--VB Wario Land (From reset, any time after)
--Teleroboxer (From reset, any time after)

local MemAddr = tonumber("050000F4", 16);
local ButtonWasPressed;

function MusicSelect()

local kbinput = input.get();

if (not ButtonWasPressed) then

 if (kbinput.pageup) then
 ButtonWasPressed = 1;
 MusicValue = memory.readbyte(MemAddr);
 MusicValue = tonumber(MusicValue) + 1;
 memory.writebyte(MemAddr,MusicValue);
 end;
 
 if (kbinput.pagedown) then
 ButtonWasPressed = 1;
 MusicValue = memory.readbyte(MemAddr);
 MusicValue = tonumber(MusicValue) - 1;
 memory.writebyte(MemAddr,MusicValue);
 end;

end;

-- Allows one to disable the boolean pretty easily
ButtonWasPressed = (kbinput.pageup or kbinput.pagedown);

end;

-- Sets the routine to run with the emulation
emu.registerafter(MusicSelect);
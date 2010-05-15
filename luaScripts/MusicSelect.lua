--Music Selection LUA Script
--Page Up to increase, Page Down to decrease
--
--This is confirmed to work in the following games:
--
--Game Num List():
--1 (Basic Testing)(VB Wario Land can use either, so it uses 2 for user convenience)
--2 VB Wario Land (From reset, any time after)
--2 Teleroboxer (From reset, any time after)

local GameNum = 2;

local InsertKeyMusSelect = 23;

local MusReadAddr = tonumber("050000F6", 16);
local MusWriteAddr = tonumber("050000F4", 16);
local ButtonWasPressed;

function MusicSelect()

local kbinput = input.get();

if (not ButtonWasPressed) then

 if (kbinput.pageup) then
 ButtonWasPressed = 1;
 MusicValue = memory.readbyte(MusReadAddr);
 MusicValue = tonumber(MusicValue) + 1;
 end;
 
 if (kbinput.pagedown) then
 ButtonWasPressed = 1;
 MusicValue = memory.readbyte(MusReadAddr);
 MusicValue = tonumber(MusicValue) - 1;
 end;

-- An easily customized music selection key,
-- if you want to jump from silence to a specific track.
 if (kbinput.insert) then
 ButtonWasPressed = 1;
 MusicValue=InsertKeyMusSelect;
 end;

 if (ButtonWasPressed) then
  if (GameNum == 2) then
   memory.writebyte(tonumber("050000D0", 16),tonumber(MusicValue)); 
  end;
  memory.writebyte(MusWriteAddr,tonumber(MusicValue));
 end;

end;

-- Allows one to disable the boolean pretty easily
ButtonWasPressed = (kbinput.pageup or kbinput.pagedown or kbinput.insert);

end;

-- Sets the routine to run with the emulation.
-- Likely to make a good base script for doing cheat codes.
emu.registerafter(MusicSelect);
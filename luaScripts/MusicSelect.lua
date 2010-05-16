--Music Selection LUA Script
--Page Up to increase, Page Down to decrease.
--This can crash your game, so you should be careful.
--
--This is confirmed to work in the following games:
--
--Game Num List():
--1 (Basic Driver)(Some can use this or 2, so they use 2 for user convenience)
--2 Galactic Pinball (From reset, any time after)
--2 Mario Clash (From reset, any time after)
--4 Panic Bomber (From reset, any time after)
--3 Red Alarm (From reset, any time after)
--2 Teleroboxer (From reset, any time after)
--2 VB Wario Land (From reset, any time after)
--5 Vertical Force (From reset, any time after)
--
--Not Done:
--3D Tetris
--Golf
--Jack Bros
--Mario's Tennis (Broken)
--Nester's Funky Bowling (Broken)
--Virtual League Baseball (Broken)

local GameNum = 5;

local InsertKeyMusSelect = 37;

local MusReadAddr = tonumber("050000F6", 16);
local MusWriteAddr = tonumber("050000F4", 16);

local StripBit80 = 0;

if (GameNum == 3) then
MusReadAddr = tonumber("05000045", 16);
MusWriteAddr = MusReadAddr;
else
 if (GameNum == 4) then
   MusReadAddr = tonumber("0500E0C9", 16);
   MusWriteAddr = MusReadAddr;
   StripBit80 = 1;
  else
   MusReadAddr = tonumber("05007DBD", 16);
   MusWriteAddr = MusReadAddr;
   StripBit80 = 1;
 end;
end;

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
  
  if (GameNum == 3) then
   memory.writebyte(tonumber("05000044", 16),1);
  end;

  if (StripBit80 == 1) then
   if (tonumber(MusicValue) > 127) then
    MusicValue = (tonumber(MusicValue) - 128)
   end;
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
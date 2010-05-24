--Music Selection LUA Script
--Page Up to increase, Page Down to decrease.
--This can crash your game, so you should be careful.
--
--This is confirmed to work in the following games:
--
--GameNum List:
--1 (Basic Driver)(Some can use this or 2, so they use 2 for user convenience)
--6 3D Tetris (From reset, any time after)
--2 Galactic Pinball (From reset, any time after)
--7 Golf (From reset, any time after)
--2 Mario Clash (From reset, any time after)
--4 Panic Bomber (From reset, any time after)
--3 Red Alarm (From reset, any time after)
--2 Teleroboxer (From reset, any time after)
--2 VB Wario Land (From reset, any time after)
--5 Vertical Force (From reset, any time after)

--User Control:
 --Use with GameNum List to select game.
 local GameNum = 2;

 --Lets you jump directly to a song. Good for starting from silence to record.
 local InsertKeyMusSelect = 37;

 --Displays the song number. May not detect tracks going to silence. 1 = on, 0 = off.
 local DisplaySongNumber = 1;

 --Lets you prevent songs from rolling below 0. 1 = on, 0 = off.
 local MakeSongZeroMinimum = 1;

--Not Done:
--Nester's Funky Bowling
--Virtual League Baseball

--Incompatible with LUA methods:
--Innsmouth Mansion (J):
--Edit the .VB file. No active music select address in RAM.
--ECCA7 = Password Screen Music
--ECCDF = Title Screen Music
--ECD73 = menu start sound
--18B30E07 = Silence
--
--Jack Bros:
--Just listening:
--Change CE2 from 16 to what you want in the .VB file
--Reload or reset the game
--Recording:
--Change 1B1D8 from 20 to what you want
--When you get to the Yes/No option, change the selection to start/restart the music
--
--Mario's Tennis:
--Open the .VB file and go to AB22
--Change A042C740 to C0A0??00, replacing ?? with the music number
--Load the game to play the music. Record, then Reset to start the music with silence
--
--Music Selection:
--ECCA3 + (# * 4) = Music (# = 0x01 - 0x4F)
--
--I'd suggest replacing the password screen music(ECCA7),
--setting ECD73 to 18B30E07,
--and ripping from the point of going into the password screen.

--Script Info:
emu.print("Gamenum is currently " .. GameNum .. ".");
emu.print("To be able to use this script with some games, you will have to Edit the script and change the Gamenum to what will work for the game you'd like.");
emu.print("");


--Declarations:
--Most prevalent music change addresses used by default
local MusReadAddr = 0x050000F6;
local MusWriteAddr = 0x050000F4;

--Hudson games use this to distinguish if a song is being asked to init, while keeping the number.
local StripBit80 = 0;

--Used to prevent button press events from registering every frame.
--Only 3 used, so no need to save a full array of all buttons to compare every frame.
local ButtonWasPressed;

if (GameNum == 3) then
 MusReadAddr = 0x05000045;
 MusWriteAddr = MusReadAddr;
else
 if (GameNum == 4) then
   MusReadAddr = 0x0500E0C9;
   MusWriteAddr = MusReadAddr;
   StripBit80 = 1;
  else
   if (GameNum == 5) then
    MusReadAddr = 0x05007DBD;
    MusWriteAddr = MusReadAddr;
    StripBit80 = 1;
   else
    if (GameNum == 6) then
     MusReadAddr = 0x05007631;
     MusWriteAddr = MusReadAddr;
    else
     if (GameNum == 7) then
      MusReadAddr = 0x05004B41;
      MusWriteAddr = MusReadAddr;
     end;
    end;
   end;
 end;
end;

function DisplayMusicVal()
 MusicValue = memory.readbyte(MusReadAddr);
 if (StripBit80 == 1) then
  if (MusicValue > 127) then
   MusicValue = (MusicValue - 128);
  end;
 end;
 gui.text(358,5,MusicValue);
end;

function MusicSelect()
--Basically a music select driver.
--May be updated with a music number display when VBJin's Lua can do it.

local kbinput = input.get();

if (not ButtonWasPressed) then

--Increase Music Value
 if (kbinput.pageup) then
  ButtonWasPressed = 1;
  MusicValue = memory.readbyte(MusReadAddr);
  MusicValue = MusicValue + 1;
 end;

--Decrease Music Value
 if (kbinput.pagedown) then
  ButtonWasPressed = 1;
  MusicValue = memory.readbyte(MusReadAddr);
  MusicValue = MusicValue - 1;
 end;

-- An easily customized music selection key,
-- if you want to jump from silence to a specific track.
 if (kbinput.insert) then
  ButtonWasPressed = 1;
  MusicValue=InsertKeyMusSelect;
 end;

--Instead of duplicating the music write code multiple times,
--it checks to see if any music change tests were run.
 if (ButtonWasPressed) then
  if (GameNum == 2) then
    memory.writebyte(0x050000D0,MusicValue); 
  else
   if (GameNum == 3) then
    memory.writebyte(0x05000044,1);
   else
    if (GameNum == 6) then
     memory.writebyte(0x05007630,1);
    else
     if (GameNum == 7) then
      memory.writebyte(0x05004B40,1);
     end;
    end;
   end;
  end;
  
  if (StripBit80 == 1) then
   if (MusicValue > 127) then
    MusicValue = (MusicValue - 128);
   end;
  end;
  memory.writebyte(MusWriteAddr,MusicValue);
 end;

end;

-- Allows one to prevent button presses from registering every frame.
-- Tests all used buttons that affect ButtonWasPressed on every frame to 0 it out when nothing is pressed.
-- A more robust solution is to save a full input array between frames,
-- and compare them for differences with the input array to be used.
ButtonWasPressed = (kbinput.pageup or kbinput.pagedown or kbinput.insert);

end;

-- Sets the routine to run with the emulation.
-- Likely to make a good base script for doing cheat codes.
emu.registerafter(MusicSelect);

-- This was not easy to deduce a need for in code.
-- gui.* stuff used for printing messages won't work unless the it's in a function that's gui.register()ed
if (DisplaySongNumber == 1) then
 gui.register(DisplayMusicVal);
end;
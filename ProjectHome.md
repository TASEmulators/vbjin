VBjin is a Virtual Boy Emulator for Windows platforms.

It comes from the merging of the [PCEjin](http://code.google.com/p/pcejin/) emulator with the [mednafen.9](http://mednafen.sourceforge.net/) Virtual Boy core.
VBjin supports rerecording and [TAS](http://tasvideos.org/) tools.


![http://dl.dropbox.com/u/75355/HostedImages/wario2.png](http://dl.dropbox.com/u/75355/HostedImages/wario2.png)
![http://dl.dropbox.com/u/75355/HostedImages/jackbros.png](http://dl.dropbox.com/u/75355/HostedImages/jackbros.png)


---

May 21, 2010: VBjin svn61 released!

Changelog:
  * ROM loading from commandline (which means movie loading now possible as well)
  * added Lua functions - memory.readbyte, memory.writebyte, memory.readword, memorywriteword
  * MusicSelect.lua - a script that allows the selecting and playing of music tracks in a game
  * Recent menu items enable sound on load (not just the open menu)
  * View->Mix Left & Right View options as well as other display options
  * Wave file logging
  * RAM Search - fix update previous values
  * RAM Search - redraw the list when search size/format is changed
  * Improved sound quality
  * Runs faster! (capable of full fps)

Most of these fixes come from ugetab, so much thanks to him.

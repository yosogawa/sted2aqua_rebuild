
# STed for Aqua - Rebuild & Restore version 
=============

"STed for Aqua," which was maintained between 2003 and 2015, was an experimental release and did not include the full feature set of STed2. Its final version only supported the Intel i386 architecture, making it incompatible with modern macOS. In 2025, I began the process of rebuilding it for contemporary environments (x86_64/arm64). Initially, this existed merely as a personal fork, but in 2026, with the recent integration of AI-assisted code analysis, the initiative has evolved into a full-scale restoration project.

## Short-term goal:
The primary objective is to restore the full functionality of STed2 on modern macOS. The goal is for the restored software to operate as a fully functional sequencer—supporting playback, input, and recording—just as the original Human68k version of STed2 did.

## Medium- to long-term goals:
Building upon this project, I aim to extend support to other operating systems. At that stage, the "STed2 for Aqua" branding will be retired. I plan to implement a hardware abstraction layer (HAL) between the hardware and the application, transforming the software into a portable system capable of running in a variety of environments.



yosogawa@gmail.com - Yoshinori Ogawa


=========================================================

## Comments from the former maintainer, Toshi Nagata, dating back to 2003

STed for Aqua
=============

  This is an experimental Cocoa/Mac OS X port of a MIDI sequencer "STed2".
The present version is barely functioinal; in particular, MIDI support is 
far from being useful. Any suggestions or patches are welcome.
  The files I have created (those in the "aqua" directories) are distributed 
with GPL2.
  For the original STed2 and its X Window version, see the "README" file.

  June 1, 2003. Toshi Nagata <alchemist_n@nifty.com>

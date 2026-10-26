
# STed for Aqua - Rebuild & Restore version 
=============

"STed for Aqua," which was maintained between 2003 and 2015, was an experimental release and did not include the full feature set of STed2. Its final version only supported the Intel i386 architecture, making it incompatible with modern macOS. In 2025, I began the process of rebuilding it for contemporary environments (x86_64/arm64). Initially, this existed merely as a personal fork, but as of 2026, it has evolved into a full-scale restoration project, leveraging AI-powered code analysis.

## Features:
Unlike the original Human68K version, which required the companion program RCD.x, and the UNIX port version, which required support for Timidity++ or aplaymidi+FluidSynth, This is a self-contained software application.  It includes a built-in sequencer and integrates two types of synthesizers.
- "ComeOn Music's ReComposer" compatible inbox sequencer (implemented by Mr. Nagata)
- Apple DLS synthesizer that supports the installation of various SoundFonts.
- "AMY," an open-source synthesizer capable of emulating the sounds of synthesizers compatible with the JUNO-106 and DX7, while also functioning as a 16-part multitimbral sound engine.
- While the original STed2 focused primarily on compatibility with the Roland GS and SC-55/SC-88/88Pro/8850 series, this version adds support for GM1, GM2, and XG. Furthermore, it now supports the "DREAM SAM2695"—a GM-compatible sound chip. Its manufacturer, DREAM, remains the only company still producing GM chips well into the 2020s.

## Short-term goal:
The primary objective is to restore the full functionality of STed2 on modern macOS. The goal is for the restored software to operate as a fully functional sequencer—supporting playback, input, and recording—just as the original Human68k version of STed2 did.

## Medium- to long-term goals:
Building upon this project, I aim to extend support to other operating systems. At that stage, the "STed for Aqua" branding will be retired. I plan to implement a hardware abstraction layer (HAL) between the hardware and the application, transforming the software into a portable system capable of running in a variety of environments.



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


# STed for Aqua - Rebuild & Restore version 
=============

"STed for Aqua," which was maintained between 2003 and 2015, was an experimental release and did not include the full feature set of STed2. Its final version only supported the Intel i386 architecture, making it incompatible with modern macOS. In 2025, I began the process of rebuilding it for contemporary environments (x86_64/arm64). Initially, this existed merely as a personal fork, but as of 2026, it has evolved into a full-scale restoration project, leveraging AI-powered code analysis.

## Features:
Unlike the original Human68K version, which required the companion program RCD.x, and the UNIX port version, which required support for Timidity++ or aplaymidi+FluidSynth, This is a self-contained software application.  It includes a built-in sequencer and integrates two types of synthesizers.

#### Sequencer & STed2 system
- "ComeOn Music's ReComposer" compatible inbox sequencer (implemented by Mr. Nagata)
- Since STed2 was originally designed to operate as a standalone system on the X68000, it lacked several fundamental sequencer functions. In this update, I have added support for slave operation via MIDI External Clock, as well as the ability to recognize MIDI real-time messages (Start, Stop, and Continue). Please note that because this implementation adheres to the MIDI real-time message specification, MIDI controllers that do not transmit MIDI clock signals are not supported. While I may consider adding such support in the future, it would require the implementation of entirely different technologies (such as HUI or MCU).
- While the original STed2 focused primarily on compatibility with the Roland GS and SC-55/SC-88/88Pro/8850 series, this version adds support for GM1, GM2, and XG. Furthermore, it now supports the "DREAM SAM2695"—a GM-compatible sound chip. Its manufacturer, DREAM, remains the only company still producing GM chips well into the 2020s.

#### DLS synth & AMY synth
- Apple DLS synthesizer that supports the installation of various SoundFonts. Files with the .sf2 or .dls extension located in the official macOS directory “~/Library/Audio/Sounds/Banks” will appear in the list.
- "AMY," an open-source synthesizer capable of emulating the sounds of synthesizers compatible with the JUNO-106 and DX7, while also functioning as a 16-part multitimbral sound engine.
- You can use up to two synths simultaneously—chosen from two DLS synths and one AMY synth—by assigning each to one of the two available MIDI outputs. Since each synth is a 16-part multitimbral sound source, you can utilize a total of up to 32 parts. Naturally, you can also route these to other MIDI devices listed under the MIDI outputs. Any built-in synth that is not in use will stop functioning when disconnected, ensuring it consumes no CPU resources.
- The AMY synth is a "headless" synthesizer. While one would typically enjoy the process of editing sounds, the current release is limited to selecting from 128 JUNO presets and 128 DX7 presets. Since the System Exclusive (SysEx) messages differ from those of the JUNO or DX7, it would be possible to enable the use of existing editors by developing a custom editor or implementing a compatibility layer for the original SysEx protocols; however, I will leave this as a task for future development.

#### Additional "Total" effects
- An integrated mixer and a set of "total effects" have been included. In addition to a reverb bus routed from the DLS synth, there is a mixer to balance the two synths, followed by three total effect modules and a final master volume control. These were chosen with mastering in mind—specifically to help tame SoundFonts that might otherwise sound harsh to the ears. The components—High/Low EQ, reverb, and a limiter—all utilize modules built into macOS. Please note that these settings, including the volume, cannot be controlled via MIDI. They can also be disabled, so feel free to use them as you prefer.

#### Requirement macOS version
- Supported OS version: macOS 12.0 Monterey or later. Compatible with both Intel and Apple Silicon.

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

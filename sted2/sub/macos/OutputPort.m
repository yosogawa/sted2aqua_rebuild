#import "OutputPort.h"
#import "InternalSynth.h"
#import "rcp_player_engine.h"

extern OSStatus CoreMIDISend(uint8_t *ptr, int length, int port, MIDITimeStamp atTime);
extern MIDIEndpointRef CoreMIDIDestinationForPort(int port);

OSStatus
OutputPortSend(uint8_t *ptr, int length, int port, MIDITimeStamp atTime)
{
	MIDIEndpointRef dest;
	OSStatus sts;

	if (ptr == NULL || length <= 0)
		return 0;
	if (port < 0)
		port = 0;
	port = port % 2;
	dest = CoreMIDIDestinationForPort(port);
	if (InternalSynthSchedule(dest, ptr, length, atTime, &sts)) {
		if (sts == noErr)
			rcpplayer_record_note(port, ptr, length);
		return sts;
	}
	return CoreMIDISend(ptr, length, port, atTime);
}

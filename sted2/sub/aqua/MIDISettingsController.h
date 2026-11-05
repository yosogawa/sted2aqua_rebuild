/* MIDISettingsController.h */

#import <Cocoa/Cocoa.h>

/* Posted on the main thread after the user changes a port or a clock flag. */
extern NSString * const MIDIPortSettingsChangedNotification;

/* Name loaded from sted2.cnf. Empty until #midi_device_input= is read. */
const char *MIDISettingsSavedInputName(void);

@interface MIDISettingsController : NSWindowController
{
	IBOutlet NSPopUpButton *midiInPopup;
	IBOutlet NSPopUpButton *midiOut1Popup;
	IBOutlet NSPopUpButton *midiOut2Popup;
	IBOutlet NSButton *clockOut1Check;
	IBOutlet NSButton *clockOut2Check;
	NSArray *deviceList;
	NSArray *inDeviceList;
	NSString *midiIn;
	NSString *midiOut1;
	NSString *midiOut2;
	BOOL midiOut1Clock;
	BOOL midiOut2Clock;
}

+ (void)showPanel;

/* Startup path. Stores the cnf value and selects the device. Does not write cnf. */
+ (void)noteInputDeviceName:(const char *)name;
+ (void)noteOutputDeviceName:(const char *)name port:(int)port;
+ (void)noteOutputClock:(int)on port:(int)port;

/* Live output name from the last cnf load or panel change. nil if unset. */
+ (NSString *)outputNameForPort:(int)port;

- (NSArray *)deviceList;
- (void)setDeviceList:(NSArray *)value;
- (NSArray *)inDeviceList;
- (void)setInDeviceList:(NSArray *)value;
- (NSString *)midiIn;
- (void)setMidiIn:(NSString *)value;
- (NSString *)midiOut1;
- (void)setMidiOut1:(NSString *)value;
- (NSString *)midiOut2;
- (void)setMidiOut2:(NSString *)value;
- (BOOL)midiOut1Clock;
- (void)setMidiOut1Clock:(BOOL)on;
- (BOOL)midiOut2Clock;
- (void)setMidiOut2Clock:(BOOL)on;

- (IBAction)midiInChanged:(id)sender;
- (IBAction)midiOut1Changed:(id)sender;
- (IBAction)midiOut2Changed:(id)sender;
- (IBAction)clockOut1Changed:(id)sender;
- (IBAction)clockOut2Changed:(id)sender;
@end

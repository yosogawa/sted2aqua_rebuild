/* MIDISettingsController.h */

#import <Cocoa/Cocoa.h>

@interface MIDISettingsController : NSWindowController
{
	IBOutlet NSButton *clockOut1Check;
	IBOutlet NSButton *clockOut2Check;
}
+ (void)showPanel;
@end

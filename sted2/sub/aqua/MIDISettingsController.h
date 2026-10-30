/* MIDISettingsController.h */

#import "MyDialogController.h"

@interface MIDISettingsController : MyDialogController
{
	IBOutlet NSButton *clockOut1Check;
	IBOutlet NSButton *clockOut2Check;
}
+ (BOOL)runDialog;
@end

/* Sf2DefGenerator.h — macOS UI entry for sf2_def_gen. */

#import <Cocoa/Cocoa.h>

@interface Sf2DefGenerator : NSObject
/* Pick an SF2 (defaults to ~/Library/Audio/Sounds/Banks), call the C
   generator, write under ~/Library/Application Support/sted/deffile/. */
+ (void)run;
@end

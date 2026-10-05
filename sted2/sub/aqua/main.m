//
//  main.m
//  STed2_aqua2
//
//  Created by Toshi Nagata on 15/03/19.
//  Copyright 2015 __MyCompanyName__. All rights reserved.
//

#import <Cocoa/Cocoa.h>

extern int isconsole;
extern int sted_saved_argc;
extern char **sted_saved_argv;
extern void sted_select_ui(int *argc, char ***argv);
extern void XSTed_setup_default_path(void);
extern int sted2_main(int argc, char **argv);

static void
sted_detach_from_gui(void)
{
    /*
      Hide a leftover MainMenu.xib window if AppKit already created it.
      Do not TransformProcessType here: that can disturb the tty that
      curses needs.
    */
    [[NSUserDefaults standardUserDefaults] setBool:YES
                                            forKey:@"ApplePersistenceIgnoreState"];
    if (NSApp != nil) {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        for (NSWindow *w in [NSApp windows])
            [w orderOut:nil];
    }
}

int main(int argc, char *argv[])
{
    sted_select_ui(&argc, &argv);
    sted_saved_argc = argc;
    sted_saved_argv = argv;
    if (isconsole) {
        sted_detach_from_gui();
        XSTed_setup_default_path();
        return sted2_main(argc, argv);
    }
    return NSApplicationMain(argc, (const char **)argv);
}

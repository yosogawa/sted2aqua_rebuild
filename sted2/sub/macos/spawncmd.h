/* Run one program from a command line. No shell.
   -1 when the line is empty, uses shell syntax, or cannot be started.
   Otherwise the wait status: 0 when the program exits 0, positive otherwise. */
int sted_spawn_command(const char *line);

#ifndef F_SNOOP_H
#define F_SNOOP_H

// F2: trace a command (or -p pid) with ptrace, summarise syscalls on exit
int snoop_run(int argc, char **argv);
#endif
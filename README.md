# OSN Mini Project 1 (C-Shell and xv6)

Part completed so far-
1. Part A
2. Part B
3. Part C

# C Shell


The prompt looks like `<user@host:cwd> `. Whatever directory you start the
shell from counts as home, so that one shows up as `~`.

## How the files are named

Everything is prefixed with the part it belongs to, `a_` for part A, `b_`
for the builtins, `c_` for execution. Headers live in `include/`, sources in
`src/`, and the Makefile just globs `src/*.c` so adding a file needs no
changes to it.

## Part A

This is the loop in `a_main.c`: print a prompt, read a line, turn it into
tokens (`a_lexer.c`), group those into jobs (`a_parser.c`), run them.

A `Job` is one pipeline. Each `Command` inside it keeps its own argv and its
own list of redirections. The lexer handles quotes and the operators
`< > >> | ; &`.

## Part B: the builtins

These four run inside the shell process itself, no `exec` anywhere.

### hop

`hop ~ . .. -` and normal paths all work, processed left to right. The
interesting bit is what happens when a name isn't a real path: instead of
failing, it looks through the directories I've visited before and jumps to
the best-ranked one whose path contains that name. So `hop osn` gets me to
`~/osnmp1` from anywhere.

Visits get written to `.cshell_hop_history` in the shell's home directory so
this survives restarts. Each line is just `visits timestamp path`. Scoring is
visits multiplied by a recency bonus:

| Last visit | Score |
|---|---|
| within the hour | visits x16 |
| within the day | visits x8 |
| within the week | visits x4 |
| older | visits |

All integers, so it's deterministic. Ties break on recency and then on the
path string. If the top match got deleted since, it's skipped and the next
one is tried. Table caps at 512 entries and drops the weakest when full.

### reveal

`ls`, basically. `-a` shows hidden files, `-t` recurses and prints each
directory as `name/` followed by its contents. Sorted by ASCII at every
level. You can spam the flags however you like, `-ta -ttaaaa` is the same as
`-at`.

Paths resolve the same way as in hop, minus the frecency fallback. Anything
that doesn't resolve gets `reveal: no such directory`.

### peek

`cat`, with `-n` for numbering non-empty lines and `-r` for reverse order.
Both together keeps the original line numbers and prints bottom-up. Multiple
files get concatenated, but each one is reversed on its own. No filename (or
`-`) reads stdin.

The `-r` case on a real file was the annoying one. The spec says use `lseek`
and read backwards in chunks instead of slurping the file, so it seeks to the
last 4096 bytes, scans that block right to left printing whole lines, and
carries whatever is left at the front into the next block, since that line
starts further back. Pipes can't seek so those get buffered, which the spec
allows.

### locate

`which`, except it checks the current directory first and then every PATH
entry, and prints *all* the matches rather than stopping at the first. No
`realpath`, so `/bin/ls` and `/usr/bin/ls` both show up.

## Part C: actually running things

All in `c_exec.c`. No `system()` or `popen()`.

### Running a command

Fork, child execs, parent waits. Name lookup:

- has a `/` in it, so it's a path, use it as-is
- plain name, check the current directory first, then PATH
- starts with `%`, skip the current directory and go straight to PATH

Nothing found gives `cshell: command not found (name)`.

Per the spec, if the line has `;` or `&` only the first group runs.

### Input redirection

All the `<` files get opened before anything else happens, so if one is
missing the command doesn't run at all and you just get
`cshell: no such file or directory`.

The catch is you can list several files but stdin is one file descriptor. So
for more than one I copy them in order into a temp file and `unlink` it right
away. The name vanishes from `/tmp` immediately but the descriptor keeps the
data alive, which means nothing is left behind even if the shell dies. Rewind
it, and that becomes stdin.

### Output redirection

`>` opens with `O_TRUNC`, `>>` with `O_APPEND`, both create with 0644. Each
one keeps its own mode, so `echo hi > f1 >> f2 > f3` truncates f1 and f3 but
appends to f2.

Same temp file trick in reverse for multiple targets. The command writes into
the temp file, and after it finishes I rewind and copy that into every target
so all of them get the full output. Can't open a file gives
`cshell: unable to create file for writing` and the command doesn't run.

Builtins were fiddly here because they run in the shell process, so I `dup`
fd 0 and 1, swap them, run, then swap back. The `fflush(stdout)` before
restoring matters, forget it and the output ends up on the terminal instead
of in the file.

### Pipes

One `pipe()` per `|`, one fork per stage, stage i's stdout into the write end
and stage i+1's stdin into the read end.

The thing that took me longest: the parent has to close every pipe descriptor
right after forking. If it holds onto a write end the reader never gets EOF
and the whole thing just hangs. Children close the ends they aren't using
too, and the parent waits on every stage, not only the last one.

Command lookup happens inside the child, which conveniently handles the "one
stage fails, the rest keep going" requirement, since the bad stage prints its
error, exits, and its pipe closes on its own.

Redirections get applied after the pipe wiring, so
`cat < in.txt | sort > out.txt` does what you'd expect. Builtins work as
pipeline stages too.


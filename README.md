# OSN Mini Project 1 — C-Shell & xv6 MLFQ


Two independent parts in one repo: a C-Shell (`c-shell/`) and an MLFQ scheduler added to
xv6 (`xv6/`). 
Completed: 
1. C-Shell Parts A–F
2. xv6 MLFQ task (build system, MLFQ policy, FIFO/RR/MLFQ comparison, and report).

---

## 1. How to run the code

### C-Shell
```
cd c-shell
make all          # produces ./shell.out in c-shell/
./shell.out
```
Compiled with the required POSIX flags (`-std=c23 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700
-Wall -Wextra -Werror`)

### xv6 (scheduler selected at compile time)
```
cd xv6
make clean; make qemu                 
# default Round Robin
make clean; make qemu SCHEDULER=FIFO  
# FIFO (non-preemptive)
make clean; make qemu SCHEDULER=MLFQ  
# MLFQ
```
Quit qemu with `Ctrl-a` then `x`. `make clean` is required whenever the scheduler flag changes,
or stale object files run the wrong policy.

Two optional flags used only for the report (off by default):

| Flag     | Effect |
| -------- | ------ |
| `LOG=1`  | (MLFQ) print one `[MLFQ] t=.. pid=.. q=.. ev=..` line per queue change (for the timeline plot) |
| `PERF=1` | print one `[METRIC] ...` line per process on exit (for the comparison table) |

---

## 2. Folder structure

```
mini-project1/
├── c-shell/
│   ├── src/                 # a_* input/lexerparser/prompt, b_* builtins, c_* exec,d_* jobs & terminal control, f_* spy/snoop
│   ├── include/             # matching headers, one per source module
│   └── Makefile             # globs src/*.c -> shell.out
├── xv6/
│   ├── kernel/              # scheduler lives in proc.c / trap.c / proc.h
│   ├── user/                # schedulertest.c and the usual user programs
│   ├── mkfs/
│   ├── Makefile             # SCHEDULER / LOG / PERF flags
│   ├── plot_mlfq.py         # section 2.3.2 timeline 
│   ├── plot_compare.py      # section 2.3.3 comparison bar chart
│   ├── report.md            # the scheduler report
│   └── *.txt, *.png         # captured logs and generated plots
├── AI-usage.pdf
└── README.md
```

File naming in `c-shell/` uses a part prefix: `a_` (input/parsing), `b_` (builtins), `c_`
(execution), `d_` (jobs, background, and terminal control for Parts D/E), `f_` (Part F: spy,
snoop). Headers in `include/`, sources in `src/`.

---

## 3. Design choices

### xv6 MLFQ
- **Compile-time policy.** The scheduler is chosen with a `-D` macro rather than at runtime, so
  each build contains exactly one policy and the round-robin path is untouched when no flag is set.
- **Queue order via a sequence number.** Instead of real linked-list queues, each process carries
  a monotonic `enter_seq`; the scheduler picks the lowest-priority-number queue and, within it, the
  smallest `enter_seq`. This gives FIFO order inside a queue and round-robin in queue 3 with far
  less bookkeeping and no separate queue data structures to keep consistent under locking.
- **Accounting split out.** CPU-time counting lives in `update_time()` called every tick for all
  schedulers, so turnaround/waiting/response are measurable for FIFO, RR, and MLFQ uniformly.
- **Logging is compile-gated.** `LOG=1`/`PERF=1`


### C-Shell


The prompt looks like `<user@host:cwd> `. Whatever directory you start the
shell from counts as home, so that one shows up as `~`.

#### How the files are named

Everything is prefixed with the part it belongs to, `a_` for part A, `b_`
for the builtins, `c_` for execution. Headers live in `include/`, sources in
`src/`, and the Makefile just globs `src/*.c`.

#### Part A

There is the loop in `a_main.c`: print a prompt, read a line, turn it into
tokens (`a_lexer.c`), group those into jobs (`a_parser.c`), run them.

A `Job` is one pipeline. Each `Command` inside it keeps its own argv and its
own list of redirections. The lexer handles quotes and the operators
`< > >> | ; &`.

#### Part B: the builtins

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
level. You can spam the flags however you like, `-at -atttt` is the same as
`-ta`.

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

#### Part C: actually running things

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


---

## 4. Assumptions

- **C-Shell:** input lines are at most 1024 characters (per spec); for `;`/`&` in Parts C, only the
  first command group is executed, as the spec requires; hop frecency is stored in
  `.cshell_hop_history` in the shell's home directory and is deterministic (integer scoring).
- **xv6:** a "tick" is one timer interrupt; logs are captured with `CPUS=1` so the timeline is a
  single-CPU trace; for the comparison, averages are taken over the three `schedulertest` **children**
  (the parent, which only waits, is excluded via `--children-only`); the boost interval is 48 ticks
  and slices are 1/4/8/16 as specified.

---

## 5. Key xv6 changes

- `Makefile` — added `SCHEDULER` (`MLFQ`/`FIFO`), `LOG`, and `PERF` flags.
- `kernel/proc.h` — added `priority`, `ticks_used`, `enter_seq`, and metrics `ctime/stime/etime/rtime`
  to `struct proc`.
- `kernel/proc.c`:
  - `allocproc` — new process placed at tail of queue 0; metrics initialised (Rule 1).
  - `scheduler` — three paths: MLFQ (strict priority + `enter_seq`), FIFO (earliest `ctime`,
    run to completion), and the original RR; response time recorded on first CPU.
  - `mlfq_tick_yield` — slice accounting, demotion on slice exhaustion (Rule 4), and preemption
    when a higher queue becomes non-empty (Rule 2).
  - `mlfq_boost` — every 48 ticks move all processes to queue 0 (Rule 7).
  - sleep/wakeup — voluntary yield keeps priority, re-tails the same queue (Rule 5).
  - `update_time` — per-tick CPU accounting for all schedulers.
  - `kexit` — records exit tick and (with `PERF`) prints the metric line.
  - `procdump` — prints queue, ticks used, sequence number, creation time (Ctrl-P).
- `kernel/trap.c` — per-scheduler tick handling (MLFQ yields via `mlfq_tick_yield`, FIFO never
  preempts, RR yields) and the 48-tick boost hook in `clockintr`.
- `user/schedulertest.c` — spawns CPU-bound, mixed, and I/O-bound children to exercise the scheduler.

---

## 6. Reproducing the report artifacts

Timeline plot (2.3.2):
```
make clean && make qemu SCHEDULER=MLFQ LOG=1 CPUS=1 | tee mlfq.txt   # run schedulertest, then quit
python3 plot_mlfq.py mlfq.txt          # -> mlfq_timeline.png
```
Comparison (2.3.3):
```
make clean && make qemu SCHEDULER=FIFO PERF=1 CPUS=1 | tee fifo_perf.txt   # run schedulertest
make clean && make qemu PERF=1 CPUS=1 | tee rr_perf.txt
make clean && make qemu SCHEDULER=MLFQ PERF=1 CPUS=1 | tee mlfq_perf.txt
python3 plot_compare.py --children-only fifo_perf.txt rr_perf.txt mlfq_perf.txt   # -> scheduler_comparison.png
```
Analysis is in `xv6/report.md`.

## 7. AI-usage.pdf

Due to the file exceeding 20 MB, I have attached the drive link to the AI-usage.pdf
[https://drive.google.com/file/d/13NzKaI3RbGn7sztceHG-de3cw0XbxPqr/view?usp=share_link](https://drive.google.com/file/d/1fXneY193SNtkLvaE_VAoQxtwDxV7hzSR/view?usp=share_link)

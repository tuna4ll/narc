# caposh

caposh is the Narc command shell. It implements the POSIX shell language and
the parts of bash that interactive users rely on every day. It is installed as
`/bin/caposh` and `/bin/sh`.

External utilities such as `ls` or `cat` are separate programs; the shell only
provides the builtins a shell is expected to own.

## Language

- simple commands, pipelines (`|`, `|&`), lists (`;`, `&`, `&&`, `||`), `!`
- compound commands: `{ }`, `( )`, `if`, `while`, `until`, `for`,
  `for (( ; ; ))`, `case` with `;;`, `;&` and `;;&`, `(( ))`, `[[ ]]`
- functions in both `name() compound` and `function name` form, `local`
- redirections: `<`, `>`, `>|`, `>>`, `<>`, `<&`, `>&`, `&>`, `&>>`, here
  documents (`<<`, `<<-`) and here strings (`<<<`)
- expansions: tilde, parameters (`${x:-w}`, `${x#p}`, `${x/p/r}`, `${x^^}`,
  `${x:o:l}`, `${!x}`, `${#x}` ...), command substitution (`$( )` and
  backquotes), arithmetic (`$(( ))`), field splitting, pathname expansion and
  ANSI-C quoting (`$'...'`)
- aliases, traps, `set -eux`, `set -o pipefail`, `noclobber`

## Interactive use

The line editor supports emacs-style movement and editing (`C-a`, `C-e`,
`C-k`, `C-u`, `C-w`, `C-y`, `M-b`, `M-f`, `M-d`), history navigation, reverse
search with `C-r`, and completion of commands, file names and variables with
`Tab`. History expansion (`!!`, `!n`, `!-n`, `!prefix`, `!$`, `^old^new`) is
available and history is kept in `~/.caposh_history`.

Job control follows the usual model: `C-z` stops the foreground job, and
`jobs`, `fg`, `bg`, `wait`, `kill %n` and `disown` manage jobs.

`PS1` accepts the bash prompt escapes (`\u`, `\h`, `\w`, `\W`, `\$`, `\t`,
`\j`, `\[`, `\]` ...).

## Startup

Login shells read `/etc/profile`, `~/.caposh_profile` and `~/.profile`.
Interactive shells read `/etc/caposhrc`, `~/.caposhrc` and the file named by
`ENV`.

## Layout

- `src/parser.c` tokenizes and parses input into the tree in `ast.h`
- `src/expand.c`, `src/arith.c` and `src/glob.c` implement word expansion
- `src/exec.c`, `src/redirect.c` and `src/functions.c` evaluate the tree
- `src/jobs.c` and `src/trap.c` handle processes, job control and signals
- `src/lineedit.c`, `src/history.c` and `src/prompt.c` provide the interactive
  front end
- `src/builtins/` contains one file per group of builtin commands

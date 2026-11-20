/*
 * whoami.h - identity and boot history.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * The command itself is documented at the top of whoami.c. This header
 * exists only so main.c can start the boot history, and so the appid string
 * has ONE definition rather than one per caller -- the shape that has cost
 * this bench a reset-reason table, a word list and a card renderer, each
 * implemented twice and each disagreeing with itself the moment one copy was
 * fixed.
 */
#ifndef APP_WHOAMI_H
#define APP_WHOAMI_H

#include <stddef.h>

/*
 * The machine-matchable identity line, without a trailing newline:
 *
 *     appid <token> [unit <n>] fw <major.minor.patch>
 *
 * The token is a CONTRACT. Scripts match on it to decide which board they
 * are holding, on a bench where every Zephyr CDC device enumerates as
 * 2FE3:0004 and picking wrong means driving the wrong hardware. Renaming it
 * breaks detection silently, which is the worst way for it to break.
 *
 * Returns the length written, or a negative errno.
 */
int whoami_app_id(char *buf, size_t cap);

/*
 * Read and clear this boot's reset cause, load the stored history, record
 * this boot, save it. Call ONCE, after the settings subsystem is up.
 *
 * WHY IT EXISTS. The cause register describes only the boot you are standing
 * in, and it must be cleared or the next boot reads a union of every boot
 * since power-on. So the question you actually have -- "why did it stop last
 * time" -- had no answer, because the act of getting the board working again
 * is the act of destroying the evidence. A sibling board lost the reason for
 * a 2026-11-18 reset exactly that way. One settings write per boot keeps it.
 */
void whoami_history_begin(void);

#endif /* APP_WHOAMI_H */

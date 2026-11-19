/*
 * The write token. Why it is provisioned rather than compiled in, and why
 * NVS holds it in the clear, is in auth.h.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

#include "auth.h"

LOG_MODULE_REGISTER(pmc_auth, LOG_LEVEL_INF);

/*
 * NOT NUL-TERMINATED, and stored with its length. The settings record holds
 * exactly the token's bytes, so a stored token of the maximum length needs no
 * room for a terminator - and comparing by length rather than by strlen keeps
 * a stray NUL inside a presented value from truncating the comparison.
 */
static char   s_tok[PMC_AUTH_TOKEN_MAX];
static size_t s_tok_len;

/*
 * The shell thread writes this and the HTTP thread reads it. Short enough to
 * hold across the compare, and the alternative - publishing a new buffer and
 * swapping a pointer - buys nothing on a path that runs once per POST.
 */
static K_MUTEX_DEFINE(s_lock);

static atomic_t s_refusals;

/* --------------------------------------------------------------- NVS ----- */

#define PMC_AUTH_KEY "pmc/tok"

static int tok_set(const char *key, size_t len, settings_read_cb read_cb,
		   void *cb_arg)
{
	char tmp[PMC_AUTH_TOKEN_MAX];
	ssize_t n;

	if (settings_name_next(key, NULL) != 0) {
		return -ENOENT;
	}

	/*
	 * A stored record outside the accepted range is REFUSED, not truncated
	 * and not padded. The only ways one can exist are a shorter minimum in
	 * an older image or a corrupt read, and in both cases a board that
	 * answers to a token nobody can name is worse than one that answers to
	 * nothing: the first looks provisioned.
	 */
	if (len < PMC_AUTH_TOKEN_MIN || len > PMC_AUTH_TOKEN_MAX) {
		LOG_WRN("stored token is %u bytes, outside %u..%u - ignored, "
			"so writes stay refused until re-provisioned",
			(unsigned)len, PMC_AUTH_TOKEN_MIN,
			PMC_AUTH_TOKEN_MAX);
		return 0;
	}

	n = read_cb(cb_arg, tmp, len);
	if (n != (ssize_t)len) {
		LOG_WRN("stored token read %d of %u bytes - ignored",
			(int)n, (unsigned)len);
		return 0;
	}

	k_mutex_lock(&s_lock, K_FOREVER);
	memcpy(s_tok, tmp, len);
	s_tok_len = len;
	k_mutex_unlock(&s_lock);

	/* The LENGTH, never the token. This line exists so that "provisioned"
	   is visible in a boot log; printing the value would undo the whole
	   point of moving it out of the image. */
	LOG_INF("write token loaded from NVS (%u characters)", (unsigned)len);
	return 0;
}

/*
 * Loaded by the settings_load_subtree("pmc") that persist.c already performs:
 * this key shares that subtree deliberately, so there is one load and one
 * place that decides when stored state is applied.
 */
SETTINGS_STATIC_HANDLER_DEFINE(pmc_tok, PMC_AUTH_KEY, NULL, tok_set, NULL,
			       NULL);

/* ------------------------------------------------------------- public ---- */

bool pmc_auth_configured(void)
{
	bool got;

	k_mutex_lock(&s_lock, K_FOREVER);
	got = s_tok_len > 0U;
	k_mutex_unlock(&s_lock);

	return got;
}

size_t pmc_auth_len(void)
{
	size_t n;

	k_mutex_lock(&s_lock, K_FOREVER);
	n = s_tok_len;
	k_mutex_unlock(&s_lock);

	return n;
}

bool pmc_auth_ok(const char *presented)
{
	size_t plen;
	uint8_t diff = 0;
	bool ok;

	if (presented == NULL) {
		return false;
	}

	plen = strlen(presented);

	k_mutex_lock(&s_lock, K_FOREVER);

	if (s_tok_len == 0U) {
		k_mutex_unlock(&s_lock);
		return false;
	}

	/*
	 * Fold the length into the difference rather than returning early on
	 * it. An early return on length is itself a timing signal, and it is
	 * free to avoid here.
	 */
	diff |= (uint8_t)((plen ^ s_tok_len) != 0U);

	for (size_t i = 0; i < s_tok_len; i++) {
		const uint8_t got = (i < plen) ? (uint8_t)presented[i] : 0U;

		diff |= (uint8_t)(got ^ (uint8_t)s_tok[i]);
	}

	ok = diff == 0U;
	k_mutex_unlock(&s_lock);

	return ok;
}

int pmc_auth_set(const char *tok)
{
	size_t len;
	int rc;

	if (tok == NULL) {
		return -EINVAL;
	}

	len = strlen(tok);
	if (len < PMC_AUTH_TOKEN_MIN || len > PMC_AUTH_TOKEN_MAX) {
		return -EINVAL;
	}

	/*
	 * FLASH FIRST, RAM SECOND. If the save fails the board keeps answering
	 * to the token it already had, which is a state somebody can still
	 * reach it in. The other order would leave it answering to a token
	 * that survives no reset and exists in no script.
	 */
	rc = settings_save_one(PMC_AUTH_KEY, tok, len);
	if (rc != 0) {
		LOG_ERR("token save: %d - nothing changed", rc);
		return rc;
	}

	k_mutex_lock(&s_lock, K_FOREVER);
	memcpy(s_tok, tok, len);
	s_tok_len = len;
	k_mutex_unlock(&s_lock);

	LOG_INF("write token provisioned (%u characters)", (unsigned)len);
	return 0;
}

int pmc_auth_clear(void)
{
	int rc = settings_delete(PMC_AUTH_KEY);

	if (rc != 0) {
		LOG_ERR("token delete: %d", rc);
		return rc;
	}

	k_mutex_lock(&s_lock, K_FOREVER);
	/*
	 * Wiped rather than merely marked empty. The buffer is static, so the
	 * bytes would otherwise sit in RAM for the rest of the boot with
	 * nothing pointing at them - which is exactly the shape of the problem
	 * this whole change is about, one storage layer down.
	 */
	memset(s_tok, 0, sizeof(s_tok));
	s_tok_len = 0U;
	k_mutex_unlock(&s_lock);

	LOG_WRN("write token cleared - every write is now refused");
	return 0;
}

uint32_t pmc_auth_refusals(void)
{
	return (uint32_t)atomic_get(&s_refusals);
}

void pmc_auth_note_refusal(void)
{
	const atomic_val_t n = atomic_inc(&s_refusals) + 1;

	LOG_WRN("write refused (%u since boot)", (unsigned)n);
}

/* -------------------------------------------------------------- shell ---- */

/*
 * THE SHELL ECHOES WHAT IT IS SENT, and CONFIG_SHELL_HISTORY keeps the line
 * afterwards, so a token typed here is visible on the console and recallable
 * with the up arrow until the next reset. That is true of `wifi cred add -p`
 * as well and is a property of provisioning over a console rather than of
 * this command. tools/token_provision.ps1 redacts its own transcript; a
 * person typing it by hand should know the line is still in the history.
 */
static int cmd_token_set(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);

	rc = pmc_auth_set(argv[1]);
	if (rc == -EINVAL) {
		shell_error(sh, "token must be %u..%u characters, got %u",
			    PMC_AUTH_TOKEN_MIN, PMC_AUTH_TOKEN_MAX,
			    (unsigned)strlen(argv[1]));
		return -EINVAL;
	}
	if (rc != 0) {
		shell_error(sh, "could not save: %d", rc);
		return rc;
	}

	shell_print(sh, "stored %u characters in NVS; it survives every OTA",
		    (unsigned)strlen(argv[1]));
	return 0;
}

static int cmd_token_clear(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = pmc_auth_clear();
	if (rc != 0) {
		shell_error(sh, "could not clear: %d", rc);
		return rc;
	}

	shell_print(sh, "cleared; the panel is read-only until re-provisioned");
	return 0;
}

static int cmd_token_status(const struct shell *sh, size_t argc, char **argv)
{
	const size_t n = pmc_auth_len();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	/* Length and refusal count only. This command exists to answer "is it
	   provisioned", and a command that answers "with what" would put the
	   token back on a console. */
	if (n == 0U) {
		shell_print(sh, "no token - every write is refused");
		shell_print(sh, "provision with: token set <value>, or "
				"tools/token_provision.ps1");
	} else {
		shell_print(sh, "provisioned, %u characters", (unsigned)n);
	}
	shell_print(sh, "refusals   %u since boot",
		    (unsigned)pmc_auth_refusals());
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_token,
	SHELL_CMD_ARG(set, NULL,
		      "token set <value> - store the write token in NVS",
		      cmd_token_set, 2, 0),
	SHELL_CMD_ARG(clear, NULL,
		      "forget the token; the panel becomes read-only",
		      cmd_token_clear, 1, 0),
	SHELL_CMD_ARG(status, NULL,
		      "whether one is provisioned, and how long it is",
		      cmd_token_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(token, &sub_token,
		   "The write token: provisioned per board, never compiled in.",
		   NULL);

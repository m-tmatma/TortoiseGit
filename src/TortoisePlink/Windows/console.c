/*
 * console.c - various interactive-prompt routines shared between
 * the Windows console PuTTY tools
 */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"
#include "storage.h"
#include "ssh.h"
#include "console.h"

#include "LoginDialog.h"

void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    sk_cleanup();

    random_save_seed();

    exit(code);
}

/*
 * System for getting I/O handles to talk to the console for
 * interactive prompts.
 *
 * In PuTTY 0.81 and before, these prompts used the standard I/O
 * handles. But this means you can't redirect Plink's actual stdin
 * from a sensible data channel without the responses to login prompts
 * unwantedly being read from it too. Also, if you have a real
 * console handle then you can read from it in Unicode mode, which is
 * an option not available for any old file handle.
 *
 * However, many versions of PuTTY have worked the old way, so we need
 * a method of falling back to it for the sake of whoever's workflow
 * it turns out to break. So this structure equivocates between the
 * two systems.
 */
static bool conio_use_standard_handles = false;
bool console_set_stdio_prompts(bool newvalue)
{
    conio_use_standard_handles = newvalue;
    return true;
}

static bool conio_use_utf8 = true;
bool set_legacy_charset_handling(bool newvalue)
{
    conio_use_utf8 = !newvalue;
    return true;
}

typedef enum {
    RESPONSE_ABANDON,
    RESPONSE_YES,
    RESPONSE_NO,
    RESPONSE_INFO,
    RESPONSE_UNRECOGNISED
} ResponseType;

/*
 * Helper function to print the message from a SeatDialogText. Returns
 * the final prompt to print on the input line, or NULL if a
 * batch-mode abort is needed. In the latter case it will have printed
 * the abort text already.
 */
static strbuf *console_print_seatdialogtext(
    SeatDialogText *text, const char **title)
{
    strbuf* buf = strbuf_new();

    assert(title);
    *title = NULL;

    for (SeatDialogTextItem *item = text->items,
             *end = item+text->nitems; item < end; item++) {
        switch (item->type) {
          case SDT_TITLE:
            *title = item->text;
            break;
          case SDT_PARA:
            put_dataz(buf, item->text);
            put_dataz(buf, "\n");
            break;
          case SDT_DISPLAY:
            put_dataz(buf, "  ");
            put_dataz(buf, item->text);
            put_dataz(buf, "\n");
            break;
          case SDT_SCARY_HEADING:
            /* Can't change font size or weight in this context */
            put_dataz(buf, item->text);
            put_dataz(buf, "\n");
            put_dataz(buf, "\n");
            break;
          case SDT_BATCH_ABORT:
            if (console_batch_mode) {
                strbuf_free(buf);
            }
            break;
          case SDT_PROMPT:
            put_dataz(buf, "\n");
            put_dataz(buf, item->text);
            break;
          default:
            break;
        }
    }
    assert(*title);
    return buf;
}

SeatPromptResult console_confirm_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, SeatDialogText *text, HelpCtx helpctx,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    int mbret;
    const char* title = NULL;

    strbuf *buf = console_print_seatdialogtext(text, &title);
    if (!buf)
        return SPR_SW_ABORT("Cannot confirm a host key in batch mode");

    mbret = MessageBoxA(GetParentHwnd(), buf->s, title, MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON3);
    strbuf_free(buf);
    if (mbret == IDYES) {
        store_host_key(seat, host, port, keytype, keystr);
        return SPR_OK;
    } else if (mbret == IDNO) {
        return SPR_OK;
    } else {
        return SPR_USER_ABORT;
    }
}

SeatPromptResult console_confirm_weak_crypto_primitive(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    int mbret;
    const char* title = NULL;
    static const char mbtitle[] = "%s Security Alert";

    strbuf *buf = console_print_seatdialogtext(text, &title);
    if (!buf)
        return SPR_SW_ABORT("Cannot confirm a weak crypto primitive "
                              "in batch mode");

    title = dupprintf(mbtitle, appname);

    mbret = MessageBoxA(GetParentHwnd(), buf->s, title, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    strbuf_free(buf);
    sfree(title);
    if (mbret == IDYES) {
        return SPR_OK;
    } else {
        return SPR_USER_ABORT;
    }
}

SeatPromptResult console_confirm_weak_cached_hostkey(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    int mbret;
    char *title = NULL;
    static const char mbtitle[] = "%s Security Alert";

    strbuf* buf = console_print_seatdialogtext(text, &title);
    if (!buf)
        return SPR_SW_ABORT("Cannot confirm a weak cached host key "
                            "in batch mode");

    title = dupprintf(mbtitle, appname);

    mbret = MessageBoxA(GetParentHwnd(), buf->s, title, MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON3);
    strbuf_to_str(buf);
    sfree(title);

    if (mbret == IDYES) {
        return SPR_OK;
    } else {
        return SPR_USER_ABORT;
    }
}

bool is_interactive(void)
{
    return is_console_handle(GetStdHandle(STD_INPUT_HANDLE));
}

bool console_antispoof_prompt = true;

void console_set_trust_status(Seat *seat, bool trusted)
{
    /* Do nothing in response to a change of trust status, because
     * there's nothing we can do in a console environment. However,
     * the query function below will make a fiddly decision about
     * whether to tell the backend to enable fallback handling. */
}

bool console_can_set_trust_status(Seat *seat)
{
    if (console_batch_mode) {
        /*
         * In batch mode, we don't need to worry about the server
         * mimicking our interactive authentication, because the user
         * already knows not to expect any.
         */
        return true;
    }

    return false;
}

bool console_has_mixed_input_stream(Seat *seat)
{
    if (!is_interactive() || !console_antispoof_prompt) {
        /*
         * If standard input isn't connected to a terminal, then even
         * if the server did send a spoof authentication prompt, the
         * user couldn't respond to it via the terminal anyway.
         *
         * We also pretend this is true if the user has purposely
         * disabled the antispoof prompt.
         */
        return false;
    }

    return true;
}

/*
 * Ask whether to wipe a session log file before writing to it.
 * Returns 2 for wipe, 1 for append, 0 for cancel (don't log).
 */
int console_askappend(LogPolicy *lp, Filename *filename,
                      void (*callback)(void *ctx, int result), void *ctx)
{
    static const char msgtemplate[] =
        "The session log file \"%.*s\" already exists.\n"
        "You can overwrite it with a new session log,\n"
        "append your session log to the end of it,\n"
        "or disable session logging for this session.\n"
        "Hit \"Yes\" to wipe the file, hit \"No\" to append to it,\n"
        "or just press \"Cancel\" to disable logging.\n"
        "Wipe the log file?";

    static const char msgtemplate_batch[] =
        "The session log file \"%.*s\" already exists.\n"
        "Logging will not be enabled.\n";

    int mbret;
    char *message, *title;
    wchar_t *wmessage, *wtitle;
    static const char mbtitle[] = "%s Session log";

    message = dupprintf(msgtemplate, FILENAME_MAX, filename->utf8path);
    title = dupprintf(mbtitle, appname);

    wmessage = decode_utf8_to_wide_string(message);
    wtitle = decode_utf8_to_wide_string(title);
    sfree(message);
    sfree(title);

    mbret = MessageBoxW(GetParentHwnd(), wmessage, wtitle, MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON3);
    sfree(wmessage);
    sfree(wtitle);

    if (mbret == IDYES)
        return 2;
    else if (mbret == IDNO)
        return 1;
    else
        return 0;
}

/*
 * Warn about the obsolescent key file format.
 *
 * Uniquely among these functions, this one does _not_ expect a
 * frontend handle. This means that if PuTTY is ported to a
 * platform which requires frontend handles, this function will be
 * an anomaly. Fortunately, the problem it addresses will not have
 * been present on that platform, so it can plausibly be
 * implemented as an empty function.
 */
void old_keyfile_warning(void)
{
    static const char message[] =
        "You are loading an SSH-2 private key which has an\n"
        "old version of the file format. This means your key\n"
        "file is not fully tamperproof. Future versions of\n"
        "PuTTY may stop supporting this private key format,\n"
        "so we recommend you convert your key to the new\n"
        "format.\n"
        "\n"
        "Once the key is loaded into PuTTYgen, you can perform\n"
        "this conversion simply by saving it again.\n";

    fputs(message, stderr);
}

/*
 * Display the fingerprints of the PGP Master Keys to the user.
 */
void pgp_fingerprints(void)
{
    fputs("These are the fingerprints of the PuTTY PGP Master Keys. They can\n"
          "be used to establish a trust path from this executable to another\n"
          "one. See the manual for more information.\n"
          "(Note: these fingerprints have nothing to do with SSH!)\n"
          "\n"
          "PuTTY Master Key as of " PGP_MASTER_KEY_YEAR
          " (" PGP_MASTER_KEY_DETAILS "):\n"
          "  " PGP_MASTER_KEY_FP "\n\n"
          "Previous Master Key (" PGP_PREV_MASTER_KEY_YEAR
          ", " PGP_PREV_MASTER_KEY_DETAILS "):\n"
          "  " PGP_PREV_MASTER_KEY_FP "\n", stdout);
}

void console_logging_error(LogPolicy *lp, const char *string)
{
    /* Ordinary Event Log entries are displayed in the same way as
     * logging errors, but only in verbose mode */
    fprintf(stderr, "%s\n", string);
    fflush(stderr);
}

void console_eventlog(LogPolicy *lp, const char *string)
{
    /* Ordinary Event Log entries are displayed in the same way as
     * logging errors, but only in verbose mode */
    if (lp_verbose(lp))
        console_logging_error(lp, string);
}

StripCtrlChars *console_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic)
{
    return stripctrl_new(bs_out, false, 0);
}

SeatPromptResult console_get_userpass_input(prompts_t *p)
{
    size_t curr_prompt;

    /*
     * Zero all the results, in case we abort half-way through.
     */
    {
        int i;
        for (i = 0; i < (int)p->n_prompts; i++)
            prompt_set_result(p->prompts[i], "");
    }

    if (console_batch_mode)
        return SPR_USER_ABORT;

    for (curr_prompt = 0; curr_prompt < p->n_prompts; curr_prompt++) {
        prompt_t *pr = p->prompts[curr_prompt];

        char result[MAX_LENGTH_PASSWORD] = { 0 };
        if (!DoLoginDialog(result, sizeof(result), pr->prompt))
            return SPR_USER_ABORT;
        prompt_set_result(pr, result);
        SecureZeroMemory(&result, sizeof(result));
    }

    return SPR_OK;
}

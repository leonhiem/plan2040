#include "kernel/jobs.h"
#include "kernel/task.h"
#include "kernel/fs.h"
#include "kernel/prog.h"
#include "kernel/errstr.h"
#include <cstdio>
#include <cstring>

// Why the last pipeline_run_once() failed, in words: "<who>: <why>",
// e.g. "cat: /dev/skintemp: ADS1115 (0x48, header bus) not answering".
static char pipe_err[JOB_ERR_MAX];

const char *pipeline_error(void)
{
    return pipe_err;
}

// Records the failure (and prints it in the foreground). Always false.
// who = 0: why already says what failed (fs errors start with the path).
static bool fail(bool foreground, const char *who, const char *why)
{
    if (!why || !why[0]) why = "failed";
    if (who) snprintf(pipe_err, sizeof(pipe_err), "%s: %s", who, why);
    else     snprintf(pipe_err, sizeof(pipe_err), "%s", why);
    if (foreground) printf("%s\n", pipe_err);
    return false;
}

bool pipeline_run_once(pipeline_t *p, bool print_if_no_redirect)
{
    bool fg = print_if_no_redirect;   // foreground: the shell, watch
    // static, not on the stack: 2 KB here made the deepest call path
    // (shell -> dispatch -> this -> a program -> printf) ~4 KB, double the
    // 2 KB stack the SDK reserves; it only worked by spilling into core 1's
    // stack area (unused so far). Safe as static: one core, cooperative
    // tasks, and nothing calls pipeline_run_once() from inside a pipeline.
    static char buf_a[STAGE_BUF];
    static char buf_b[STAGE_BUF];
    char *cur = buf_a, *other = buf_b;
    int cur_len = 0;

    if (p->has_redirect_in) {
        const char *in_path = pipeline_word(p, p->redirect_in);
        int fd = fs_open(in_path);
        if (fd < 0) return fail(fg, 0, errstr());
        int n = fs_read(fd, cur, STAGE_BUF);
        fs_close(fd);
        if (n < 0) return fail(fg, 0, errstr());
        cur_len = n;
    }

    for (int i = 0; i < p->nstages; i++) {
        pipeline_stage_t *st = &p->stages[i];
        if (st->argc == 0) return fail(fg, "pipeline", "empty stage");

        const char *name = pipeline_arg(p, i, 0);
        const program_t *prog = prog_find(name);
        if (!prog) return fail(fg, name, "not found (try 'ls')");

        char *argv_rest[JOB_MAX_ARGS];
        // Programs only read their arguments (none writes into them), so
        // they can point straight into the pipeline's text.
        for (int a = 1; a < st->argc; a++) argv_rest[a - 1] = (char *)pipeline_arg(p, i, a);

        errstr_clear();
        int n = prog->run(cur, cur_len, st->argc - 1, argv_rest, other, STAGE_BUF);
        if (n < 0) return fail(fg, name, errstr());

        char *tmp = cur; cur = other; other = tmp;
        cur_len = n;
    }

    if (p->has_redirect_out) {
        const char *out_path = pipeline_word(p, p->redirect_out);
        int fd = fs_open(out_path, p->redirect_append ? FS_APPEND : FS_CREATE);
        if (fd < 0) return fail(fg, 0, errstr());
        int n = fs_write(fd, cur, cur_len);
        fs_close(fd);
        // E.g. a full file, a bad value for a device. A background job
        // gets it back and reports it once (run_slot()), so a job retrying
        // every JOB_POLL_MS can't flood the terminal.
        if (n < 0) return fail(fg, 0, errstr());
    } else if (print_if_no_redirect) {
        for (int i = 0; i < cur_len; i++) putchar(cur[i]);
        // No output, no line: `rm`, a `grep` without matches, `cat` of an
        // empty file print nothing, as in Unix. (picoos printed an empty
        // line for those.) Output without a final '\n' still gets one.
        if (cur_len > 0 && cur[cur_len - 1] != '\n') putchar('\n');
    }
    return true;
}

typedef struct {
    bool active;
    pipeline_t p;
    bool failing;               // the last run failed ...
    char err[JOB_ERR_MAX];      // ... and why (pipeline_error()), for `jobs`
} job_slot_t;

static job_slot_t slots[MAX_JOBS];

// A background job runs every JOB_POLL_MS. When it starts failing, it
// says why once ("[7] cat: /dev/skintemp: ..."), again only if the reason
// changes, and once when it works again ("[7] ok again"); `jobs` shows
// the reason meanwhile. Leon's rule (2026-11-23): clear errors, and no
// flood that makes the console unusable exactly when something's wrong.
static void run_slot(int i)
{
    job_slot_t *j = &slots[i];
    if (!j->active) return;
    bool ok = pipeline_run_once(&j->p, false); // background: no output without a > redirect
    if (!ok) {
        if (!j->failing || strcmp(j->err, pipeline_error()) != 0) {
            snprintf(j->err, sizeof(j->err), "%s", pipeline_error());
            printf("[%d] %s\n", i + 1, j->err);
        }
        j->failing = true;
    } else if (j->failing) {
        j->failing = false;
        printf("[%d] ok again\n", i + 1);
    }
}

// One trampoline per job slot, generated mechanically -- see jobs.h's
// header comment for why each slot still needs its own distinct
// function. JOB_SLOTS lists 32 of them; only the first MAX_JOBS (set
// by the config) get registered as tasks, the rest are never called.
// Each registered slot is a task, so TASK_MAX (kernel/task.cpp) must
// hold MAX_JOBS + the other tasks (cpu_rp2040: shell, hours, cron).
#define JOB_SLOTS \
    X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  X(8)  X(9) \
    X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) \
    X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) \
    X(30) X(31)
static_assert(MAX_JOBS >= 1 && MAX_JOBS <= 32, "MAX_JOBS: 1..32 (JOB_SLOTS)");

#define X(n) static void job_task_##n(void) { run_slot(n); }
JOB_SLOTS
#undef X

void jobs_init(void)
{
    for (int i = 0; i < MAX_JOBS; i++) slots[i].active = false;
#define X(n) if (n < MAX_JOBS) task_register("job" #n, job_task_##n, JOB_POLL_MS);
    JOB_SLOTS
#undef X
}

int job_start(const pipeline_t *p)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!slots[i].active) {
            slots[i].p = *p;
            slots[i].active = true;
            slots[i].failing = false;
            return i + 1;
        }
    }
    return -1;
}

void job_list(void)
{
    bool any = false;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!slots[i].active) continue;
        any = true;
        printf("[%d] ", i + 1);
        for (int s = 0; s < slots[i].p.nstages; s++) {
            if (s > 0) printf("| ");
            for (int a = 0; a < slots[i].p.stages[s].argc; a++) {
                // Words with spaces or shell characters were quoted when
                // typed: show them quoted, so the listing can be typed again.
                const char *w = pipeline_arg(&slots[i].p, s, a);
                bool q = !*w || strpbrk(w, " \t|<>&#'\"");
                printf(q ? (strchr(w, '"') ? "'%s' " : "\"%s\" ") : "%s ", w);
            }
        }
        if (slots[i].p.has_redirect_out)
            printf("%s %s ", slots[i].p.redirect_append ? ">>" : ">", pipeline_word(&slots[i].p, slots[i].p.redirect_out));
        printf("&\n");
        if (slots[i].failing) printf("    failing: %s\n", slots[i].err);
    }
    if (!any) printf("no background jobs\n");
}

bool job_kill(int id)
{
    if (id < 1 || id > MAX_JOBS || !slots[id - 1].active) return false;
    slots[id - 1].active = false;
    return true;
}

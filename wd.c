/******************************************************************************
 * Author: Ofek Tal                                                           *
 * Reviewer: Atai                                                             *
 * Date: 30.01.21                                                             *
 * Description: implementations of Watch Dog                                  *
 *                                                                            * 
 * Infinity Labs OL113                                                        *
 ******************************************************************************/

#define _GNU_SOURCE

#include <stdio.h>   /* stdout sprintf()*/
#include <pthread.h> /* pthread_create()*/
#include <signal.h>  /* kill, sigaction, pid_t, struct sigaction */
#include <unistd.h>  /* fork() */
#include <assert.h>  /* assert() */
#include <stdlib.h>  /* sleep() getenv() setenv() */
#include <time.h>    /* time() clock()*/
#include <string.h>  /*strcpy() strcmp()*/

#include "wd.h"
#include "st_scheduler.h"
#include "process_semaphore.h"

/************* Macro's *************/
#define INIT_VAL (0)
#define TIME_OUT (10)
#define MAX_APP_START_LEN (100)
#define INTERVAL_TASK1 (1)
#define TIME_INTERVAL_FACTOR (5)
#define INTERVAL_TASK2 (INTERVAL_TASK1 * TIME_INTERVAL_FACTOR)
#define UNUSED_PARAM __attribute__((unused))
#define SEM_MAX_NAME (11)
#define READY_SEMAPHORE "READY_SEM"
#define RESTART_SEMAPHORE "RESTART_SEM"

/************* Type Definisions *************/
typedef enum sema_pos
{
    WAIT,
    POST
} sema_pos_t;

typedef enum exit_status
{
    SUCCESS,
    SYSTEM_ERROR
} exit_status_t;

/************* Global Variables *************/
int g_i_am_app = 1;

static sigset_t g_sig_set = {0};

static sema_pos_t g_ready_sem_position = 0;
static pid_t g_pid_partner = 0;

static int g_keep_running_task1 = 1;
static int g_keep_running_task2 = 1;

static int g_got_sigusr2 = 0;
static size_t g_counter_sigusr1 = 1;

static sem_id_t *g_sem_partner_init = NULL;
static sem_id_t *g_sem_restart = NULL;
static pthread_t g_thread = {0};

/****************************** Static Funcs ******************************/

/************* App Related Funcs *************/
static void AppSigusr1Handler(UNUSED_PARAM int signum);
static exit_status_t CreateApp(char **argv);
static void *RunAppWD(void *scheduler);

/************* WD Related Funcs *************/
static exit_status_t CreateWDProcess(char **argv);
static void WDSigusr1Handler(UNUSED_PARAM int signum);
static void *RunWDBackground(void *scheduler);
static int HasToCreateWD(void);

/************* Scheduler Funcs *************/
static int AppCheckPartnerAlive(void *argv);
static int WDCheckPartnerAlive(void *argv);
static void StopCheckPartnerAlive(void);
static void StopSendSignal(void);
static void StopTasks(void);
static int SendSignal(void *scheduler);

/************* Init Funcs *************/
static exit_status_t InitHandlers(void);
static exit_status_t InitScheduler(st_scheduler_t *scheduler, char **argv);
static exit_status_t InitSemaphores(void);
static exit_status_t InitAllResources(st_scheduler_t **scheduler_p, char **argv);
static int InitSpecificSemaphore(char *dest, char *env_sem_name);

/************* Common Funcs *************/
static int IsTimeOut(size_t time_out);
static void CleanSemaphores(void);
static void CleanAllResources(st_scheduler_t *scheduler);
static void SemaActionByPos(sema_pos_t position);
static void CpyArgv(char **dest, char *const *src);
static size_t CreateNewUid(void);
static int CreatePartnerProcess(char **argv);
static void Sigusr2Handler(UNUSED_PARAM int signum);
static int GenerateSemName(char *dest);
static void UnBlockSIGUSR(void);
static void BlockSIGUSR(void);

/****************************** API Funcs ******************************/

int WDStart(char **argv)
{
    exit_status_t status = SUCCESS;
    st_scheduler_t *scheduler = NULL;

    BlockSIGUSR();

    if (InitAllResources(&scheduler, argv))
    {
        return SYSTEM_ERROR;
    }

    if (g_i_am_app)
    {
        if (HasToCreateWD())
        {
            g_ready_sem_position = WAIT;

            if (SYSTEM_ERROR == CreateWDProcess(argv))
            {
                CleanAllResources(scheduler);

                return SYSTEM_ERROR;
            }
        }
        else
        {
            g_ready_sem_position = POST;
            g_pid_partner = getppid();
        }

        status = pthread_create(&g_thread, NULL, RunAppWD, scheduler);
    }
    else
    {
        g_pid_partner = getppid();

        RunWDBackground(scheduler);
        CleanAllResources(scheduler);
    }

    return status;
}

void WDStop(void)
{
    size_t time_out = time(NULL) + TIME_OUT;

    StopCheckPartnerAlive();

    while (!IsTimeOut(time_out) && !g_got_sigusr2)
    {
        kill(g_pid_partner, SIGUSR2);
        sleep(1);
    }

    StopSendSignal();
    pthread_join(g_thread, NULL);
}

/****************************** Static Funcs ******************************/

static void CleanSemaphores(void)
{
    SemDestroy(g_sem_restart);
    SemDestroy(g_sem_partner_init);
}

static void CleanAllResources(st_scheduler_t *scheduler)
{
    CleanSemaphores();
    STSchedulerDestory(scheduler);
}

static void StopCheckPartnerAlive(void)
{
    g_keep_running_task2 = 0;
}

static void StopSendSignal(void)
{
    g_keep_running_task1 = 0;
}

static void StopTasks(void)
{
    StopSendSignal();
    StopCheckPartnerAlive();
}

static void CpyArgv(char **dest, char *const *src)
{
    while (*src)
    {
        *dest = *src;

        dest++;
        src++;
    }
}

static int CreatePartnerProcess(char **argv)
{
    g_pid_partner = fork();

    if (-1 == g_pid_partner)
    {
        return SYSTEM_ERROR;
    }
    else if (0 == g_pid_partner)
    {
        if (g_i_am_app)
        {
            char *new_argv[MAX_APP_START_LEN] = {0};

            new_argv[0] = "./wd_bg_p";
            CpyArgv(&new_argv[1], argv);
            execvp(new_argv[0], new_argv);
        }
        else
        {
            execvp(argv[1], &argv[1]);
        }

        return SYSTEM_ERROR;
    }

    return SUCCESS;
}

static exit_status_t CreateWDProcess(char **argv)
{
    return CreatePartnerProcess(argv);
}

static void AppSigusr1Handler(UNUSED_PARAM int signum)
{
    fprintf(stdout, "APP Got ping\n");
    g_counter_sigusr1++;
}

static void WDSigusr1Handler(UNUSED_PARAM int signum)
{
    fprintf(stdout, "WD Got ping\n");
    g_counter_sigusr1++;
}

static void Sigusr2Handler(UNUSED_PARAM int signum)
{
    g_got_sigusr2 = 1;
    /*fprintf(stdout, "Got SigUsr2\n");*/
    StopTasks();
    kill(g_pid_partner, SIGUSR2);
}

static exit_status_t InitHandlers(void)
{
    struct sigaction sig1 = {0};
    struct sigaction sig2 = {0};

    sig2.sa_handler = Sigusr2Handler;

    if (g_i_am_app)
    {
        sig1.sa_handler = AppSigusr1Handler;
    }
    else
    {
        sig1.sa_handler = WDSigusr1Handler;
    }

    if (-1 == sigaction(SIGUSR1, &sig1, NULL) ||
        -1 == sigaction(SIGUSR2, &sig2, NULL))
    {
        return SYSTEM_ERROR;
    }

    return SUCCESS;
}

static int SendSignal(void *scheduler)
{
    if (g_keep_running_task1)
    {
        kill(g_pid_partner, SIGUSR1);

        return 0;
    }

    STSchedulerStop((st_scheduler_t *)scheduler);

    return 1;
}

static exit_status_t CreateApp(char **argv)
{
    return CreatePartnerProcess(argv);
}

static int AppCheckPartnerAlive(void *argv)
{
    if (g_keep_running_task2)
    {
        fprintf(stdout, "APP Counter is %ld\n", g_counter_sigusr1);

        if (g_counter_sigusr1 > 0)
        {
            g_counter_sigusr1 = 0;
        }
        else
        {
            if (SYSTEM_ERROR == CreateWDProcess((char **)argv))
            {
                StopSendSignal();

                return SYSTEM_ERROR;
            }

            SemWait(g_sem_partner_init);
        }

        return 0;
    }

    return 1;
}

static int WDCheckPartnerAlive(void *argv)
{
    if (g_keep_running_task2)
    {
        fprintf(stdout, "WD Counter is %ld\n", g_counter_sigusr1);

        if (g_counter_sigusr1 > 0)
        {
            g_counter_sigusr1 = 0;
        }
        else
        {
            SemPost(g_sem_restart);

            if (SYSTEM_ERROR == CreateApp((char **)argv))
            {
                StopSendSignal();

                return SYSTEM_ERROR;
            }

            SemWait(g_sem_partner_init);
        }

        return 0;
    }

    return 1;
}

static exit_status_t InitScheduler(st_scheduler_t *scheduler, char *argv[])
{
    exit_status_t status = SUCCESS;

    status = (UidIsMatch(BadUID, STSchedulerAdd(scheduler, SendSignal, INTERVAL_TASK1, scheduler)));

    if (g_i_am_app)
    {
        status = (UidIsMatch(BadUID, STSchedulerAdd(scheduler, AppCheckPartnerAlive, INTERVAL_TASK2, argv)));
    }
    else
    {
        status = (UidIsMatch(BadUID, STSchedulerAdd(scheduler, WDCheckPartnerAlive, INTERVAL_TASK2, argv)));
    }

    if (status)
    {
        STSchedulerDestory(scheduler);
    }

    return status;
}

static size_t CreateNewUid(void)
{
    unique_id_t uid = UidCreate();

    return uid.time + uid.pid + uid.count;
}

static int GenerateSemName(char *dest)
{
    size_t uid = CreateNewUid();

    if (0 > sprintf(dest, "/%lu", uid))
    {
        return SYSTEM_ERROR;
    }

    return SUCCESS;
}

static int InitSpecificSemaphore(char *dest, char *env_sem_name)
{
    char *env_sem = getenv(env_sem_name);
    sem_id_t *check_valid = NULL;

    if (!env_sem)
    {
        char sem_name[SEM_MAX_NAME] = {0};

        if (GenerateSemName(sem_name))
        {
            return SYSTEM_ERROR;
        }

        if (-1 == setenv(env_sem_name, sem_name, 0))
        {
            return SYSTEM_ERROR;
        }

        strcpy(dest, sem_name);
    }
    else
    {
        strcpy(dest, env_sem);
    }

    if (!strcmp(RESTART_SEMAPHORE, env_sem_name))
    {
        g_sem_restart = SemCreate(dest, INIT_VAL, POSIX);
        check_valid = g_sem_restart;
    }
    else
    {
        g_sem_partner_init = SemCreate(dest, INIT_VAL, POSIX);
        check_valid = g_sem_partner_init;
    }

    if (!check_valid)
    {
        return SYSTEM_ERROR;
    }

    return SUCCESS;
}

static exit_status_t InitSemaphores(void)
{
    static char ready_sema_name[SEM_MAX_NAME] = {0};
    static char restart_sema_name[SEM_MAX_NAME] = {0};

    if (InitSpecificSemaphore(restart_sema_name, RESTART_SEMAPHORE) ||
        InitSpecificSemaphore(ready_sema_name, READY_SEMAPHORE))
    {
        CleanSemaphores();

        return SYSTEM_ERROR;
    }

    return SUCCESS;
}

static exit_status_t InitAllResources(st_scheduler_t **scheduler_p, char **argv)
{
    st_scheduler_t *scheduler = NULL;

    if (InitHandlers() || InitSemaphores())

    {
        return SYSTEM_ERROR;
    }

    if (!(scheduler = STSchedulerCreate()) ||
        InitScheduler(scheduler, argv))
    {
        CleanSemaphores();

        return SYSTEM_ERROR;
    }

    *scheduler_p = scheduler;

    return SUCCESS;
}

static int HasToCreateWD(void)
{
    assert(g_sem_restart);

    if (1 == SemGetVal(g_sem_restart))
    {
        SemWait(g_sem_restart);

        return 0;
    }

    return 1;
}

static void SemaActionByPos(sema_pos_t position)
{
    if (POST == position)
    {
        SemPost(g_sem_partner_init);
    }
    else
    {
        SemWait(g_sem_partner_init);
    }
}

static void *RunAppWD(void *scheduler)
{
    UnBlockSIGUSR();
    SemaActionByPos(g_ready_sem_position);

    STSchedulerRun((st_scheduler_t *)scheduler);
    CleanAllResources((st_scheduler_t *)scheduler);

    return NULL;
}

static void *RunWDBackground(void *scheduler)
{
    UnBlockSIGUSR();
    SemPost(g_sem_partner_init);

    STSchedulerRun((st_scheduler_t *)scheduler);

    return NULL;
}

static int IsTimeOut(size_t time_out)
{
    return (size_t)time(NULL) >= time_out;
}

static void BlockSIGUSR(void)
{
    sigemptyset(&g_sig_set);
    sigaddset(&g_sig_set, SIGUSR1);
    sigaddset(&g_sig_set, SIGUSR2);

    sigprocmask(SIG_BLOCK, &g_sig_set, NULL);
}

static void UnBlockSIGUSR(void)
{
    pthread_sigmask(SIG_UNBLOCK, &g_sig_set, NULL);
}

/* 
 * tsh - A tiny shell program with job control
 * 
 * <Put your name and login ID here>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */ //ǰ̨
#define BG 2    /* running in background */ //��̨
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline); //�����ͽ���������������
int builtin_cmd(char **argv); //ʶ��ͽ�����������
void do_bgfg(char **argv); //ʵ��bg��fg��������
void waitfg(pid_t pid); //�ȴ�ǰ̨��ҵ���

void sigchld_handler(int sig); //����SIGCHILD�ź�
void sigtstp_handler(int sig); //����ctrl-c�ź�
void sigint_handler(int sig);  //����ctrl-z�ź�

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); //��ȡ�����б������Ƿ�Ϊ��̨��������
void sigquit_handler(int sig); //����SIGQUIT�ź�
void clearjob(struct job_t *job); //���job�ṹ��
void initjobs(struct job_t *jobs); //��ʼ������jobs
int maxjid(struct job_t *jobs); //����jobs����������jid��
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);//��jobs[]���������
int deletejob(struct job_t *jobs, pid_t pid); //��jobs[]��ɾ��pid��job
pid_t fgpid(struct job_t *jobs); //���ص�ǰǰ̨���е�job��pid��
struct job_t *getjobpid(struct job_t *jobs, pid_t pid); //����pid�ҵ���Ӧ��job
struct job_t *getjobjid(struct job_t *jobs, int jid);  //����jid�ҵ���Ӧ��job
int pid2jid(pid_t pid); //����pid�ҵ�jid
void listjobs(struct job_t *jobs); //��ӡjobs

void usage(void); //��ʾ�����ʹ�÷���
void unix_error(char *msg); //UNIXϵͳ����ʧ��ʱ���������
void app_error(char *msg); //�����������Ĵ�����Ϣ
typedef void handler_t(int); 
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
    char *argv[MAXARGS]; //�����б�
    char buf[MAXLINE]; //�����޸ĵ�������(������)
    pid_t pid; //����id
    int bg; //���job�Ƿ��ں�̨���� 
    strcpy(buf,cmdline); //�������м��ص�������
    bg = parseline(buf,argv); //���������������ȡargv����������Ĳ����б�
    //addjob(jobs,pid,bg ? BG : FG,cmdline); //����������ӵ������б���

    
    if(argv[0] == NULL){
        return; //������
    }

    sigset_t mask_all,mask_one,prev_one;
    if(!builtin_cmd(argv)){ //�ж��Ƿ�Ϊ��������

        sigfillset(&mask_all);//���浱ǰ�������źż��ϣ�blockedλ������
        sigemptyset(&mask_one);//��ʼ��mask_oneΪ�ռ�
        sigaddset(&mask_one,SIGCHLD);//���SIGCHLD��mask_one��
        //���ϱ����˵�ǰ�Ѿ��������źż���

        //���mask_one�е��źŵ��źż��ϣ��Ӷ������̱���SIGCHLD������
        sigprocmask(SIG_BLOCK,&mask_one,&prev_one);
        if((pid = fork()) == 0){ //�ӽ���
            sigprocmask(SIG_SETMASK,&prev_one,NULL);
            //��Ϊ�ӽ��̼̳������Ǹ����̵ı��������ϣ������ڵ���execve֮ǰ�������ӽ��̶�SIGCHLD������

            if(setpgid(0,0) < 0){//���ӽ��̷ŵ�һ���µĽ������У��ý�����ID���ӽ���PID��ͬ�����ȷ����ֻ��һ��shell����
                printf("setpgid error");
                exit(0);
            }

            if(execve(argv[0],argv,environ) < 0){
            //���ӽ����е���execveѰ��·���µĿ�ִ���ļ������ж��Ƿ�����ҵ�
            printf("%s: Command not found\n",argv[0]);
            exit(0);
            //test14�еĵ�һ������ֱ�����������ʾ��Ϣ
            }
                       
        }

        sigprocmask(SIG_BLOCK,&mask_all,NULL); //�ָ��źż���
        addjob(jobs,pid,bg==1 ? BG : FG,cmdline); //����������ӵ������б���
        sigprocmask(SIG_SETMASK,&prev_one,NULL);//����ӽ��̶�SIGCHLD������

        //�����̵ȴ�ǰ̨�������
        if(!bg){//������Ǻ�̨���̣��͵ȴ���ǰ��ǰ̨����ִ��
            waitfg(pid);
        }
        else{//�Ǻ�̨���̣����ں�̨����
            printf("[%d] (%d) %s",pid2jid(pid),pid,cmdline);
        }
    }
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
    if(!strcmp(argv[0],"quit")){//�������������quit
        exit(0);//������ǰ����
    }
    if(!strcmp(argv[0],"jobs")){
        listjobs(jobs);
        return 1;
    }
    if(!strcmp(argv[0],"&")){
        return 1;//����1����Ϊһ��������&��β��shell���ں�̨����
    }
    if(!strcmp(argv[0],"bg")){//ʵ��bg����ָ��
        do_bgfg(argv);
        return 1;
    }
    if(!strcmp(argv[0],"fg")){//ʵ��fg����ָ��
        do_bgfg(argv);
        return 1;
    }
    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
 
    pid_t pid;                      /* ����id */
    int jid;                        /* job��id */
    struct job_t * job;
    if (argv[1] == NULL){
        printf("%s command requires PID or %%jobid argument\n",argv[0]);
        return;
    }
    //�ڶ���������û�д���pid����jid��Ϊ�գ����ͱ�������
 
    if (argv[1][0] == '%'){   /* ����������jid����ҵ�� */       
   //���������������Ǵ����ˣ����Ǵ�������ݲ��ǲ�����pid��jid�Ĺ淶���������Ϊ���֣�
   //�������жϲ����������Ϣ��fg: argument must be a PID or %%jobid\n
        jid = atoi(argv[1]+1);
        job = getjobjid(jobs,jid);//ͨ��jid�ҵ���Ҫִ�е�job
        if(job == NULL){
            printf("%%%d: No such job\n",jid);
            return;
        }
   //���ĸ��������ͨ��jid�ҵ���job==null����ˡ�NO such job��
        pid = job->pid;
    }
    else if(argv[1][0]>='a' && argv[1][0]<='z'){
        if(argv[0][0] == 'b' ){
            printf("bg: argument must be a PID or %%jobid\n");
            return;
        }
        else if(argv[0][0] == 'f' ){
            printf("fg: argument must be a PID or %%jobid\n");
            return;
        }
        pid = atoi(argv[1]);
        job = getjobpid(jobs,pid);
        jid = job->jid;
    }
    else
    {                              /* ������pid */
        pid = atoi(argv[1]);
        job = getjobpid(jobs,pid);
        if(job == NULL){
            printf("(%d): No such process\n",pid);
            return;
        }
//������������ͨ��jid�ҵ���job==null����ˡ�NO such job��
        jid = job->jid;
    }
    if(pid > 0){
        if(!strcmp(argv[0],"bg")){          /* bg����ָ�� */
            printf("[%d] (%d) %s",jid,pid,job->cmdline);
            job->state = BG;                /* ����״̬ */
            kill(-pid,SIGCONT);              /* ����SIGCONT�źŸ��������е����н��� */
            
        }else
        if(!strcmp(argv[0],"fg")){          /* fg����ָ�� */
            job->state = FG;                /* ����״̬ */
            kill(-pid,SIGCONT);             /* ����SIGCONT�źŸ��������е����н��� */
            waitfg(pid);                    /* �ȴ�ǰ̨job��� */
        }
    }
 
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    /*
    Ψһ��ǰ̨��ҵ�����󣬱�sigchild_handler���գ�deletejob֮��,jobs�б����û��ǰ̨��ҵ��
    ����ѭ��fgpid
    */
    while(pid == fgpid(jobs)){
        sleep(0);
        //ע��sleep(0)������˯0�룬���Ƿ���ʱ��Ƭ
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
    int olderrno = errno;
    sigset_t mask_all,prev_all;
    pid_t pid;
    int status;

    sigfillset(&mask_all); //���浱ǰ�������źż��ϵ�mask_all��
    while((pid=waitpid(-1,&status,WNOHANG | WUNTRACED)) > 0){//WNOHANGΪ�������ģ�ֻ�в��������ĲŽ���deletejob
        if(WIFEXITED(status)){
            sigprocmask(SIG_BLOCK,&mask_all,&prev_all);//�ָ��źż��ϣ�ɾ����ʱ����
            deletejob(jobs,pid);//�ͷŵ������ʬ����
            sigprocmask(SIG_SETMASK,&prev_all,NULL);//ɾ��֮��ͽӴ��ӽ��̶���SIGCHLD������
        }
        //�ӽ�������Ϊһ��δ��������ź���ֹ��(SIGINT)
        if(WIFSIGNALED(status)){
            int jid = pid2jid(pid);
            printf("Job [%d] (%d) terminated by signal %d\n",jid,pid,WTERMSIG(status));
            deletejob(jobs,pid);//��ֹ��ɾ��pid��job
        }
        //���𷵻ص��ӽ��̵�ǰ��ֹͣ��(SIGTSTP)
        if(WIFSTOPPED(status)){
            struct job_t * job = getjobpid(jobs,pid);
            int jid = pid2jid(pid);
            printf("Job [%d] (%d) stopped by signal %d\n",jid,pid,WSTOPSIG(status));
            job->state = ST;
        }

    }
    errno = olderrno;
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    pid_t pid = fgpid(jobs); //��ȡǰ̨����id
    if(pid > 0){
        kill(-pid,sig);//ת���ź�sig��������|pid|�е�ÿ������
    }
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    pid_t pid = fgpid(jobs);//��ȡǰ̨����id
    if(pid > 0){
        kill(-pid,sig);//ת���ź�sig��������|pid|�е�ÿ������
    }
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}




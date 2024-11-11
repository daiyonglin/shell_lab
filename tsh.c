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
#define FG 1    /* running in foreground */ //前台
#define BG 2    /* running in background */ //后台
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
void eval(char *cmdline); //解析和解释命令行主例程
int builtin_cmd(char **argv); //识别和解释内置命令
void do_bgfg(char **argv); //实现bg和fg内置命令
void waitfg(pid_t pid); //等待前台作业完成

void sigchld_handler(int sig); //捕获SIGCHILD信号
void sigtstp_handler(int sig); //捕获ctrl-c信号
void sigint_handler(int sig);  //捕获ctrl-z信号

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); //获取参数列表，返回是否为后台运行命令
void sigquit_handler(int sig); //处理SIGQUIT信号
void clearjob(struct job_t *job); //清除job结构体
void initjobs(struct job_t *jobs); //初始化任务jobs
int maxjid(struct job_t *jobs); //返回jobs链表中最大的jid号
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);//向jobs[]中添加任务
int deletejob(struct job_t *jobs, pid_t pid); //在jobs[]中删除pid的job
pid_t fgpid(struct job_t *jobs); //返回当前前台运行的job的pid号
struct job_t *getjobpid(struct job_t *jobs, pid_t pid); //根据pid找到对应的job
struct job_t *getjobjid(struct job_t *jobs, int jid);  //根据jid找到对应的job
int pid2jid(pid_t pid); //根据pid找到jid
void listjobs(struct job_t *jobs); //打印jobs

void usage(void); //显示程序的使用方法
void unix_error(char *msg); //UNIX系统调用失败时，调用这个
void app_error(char *msg); //处理程序自身的错误信息
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
    char *argv[MAXARGS]; //参数列表
    char buf[MAXLINE]; //保存修改的命令行(缓冲区)
    pid_t pid; //进程id
    int bg; //标记job是否在后台运行 
    strcpy(buf,cmdline); //把命令行加载到缓冲区
    bg = parseline(buf,argv); //从输入的命令中提取argv，就是上面的参数列表
    //addjob(jobs,pid,bg ? BG : FG,cmdline); //将子任务添加到任务列表中

    
    if(argv[0] == NULL){
        return; //空命令
    }

    sigset_t mask_all,mask_one,prev_one;
    if(!builtin_cmd(argv)){ //判断是否为内置命令

        sigfillset(&mask_all);//保存当前的阻塞信号集合（blocked位向量）
        sigemptyset(&mask_one);//初始化mask_one为空集
        sigaddset(&mask_one,SIGCHLD);//添加SIGCHLD到mask_one中
        //以上保存了当前已经阻塞的信号集合

        //添加mask_one中的信号到信号集合，从而父进程保持SIGCHLD的阻塞
        sigprocmask(SIG_BLOCK,&mask_one,&prev_one);
        if((pid = fork()) == 0){ //子进程
            sigprocmask(SIG_SETMASK,&prev_one,NULL);
            //因为子进程继承了他们父进程的被阻塞集合，所以在调用execve之前必须解除子进程对SIGCHLD的阻塞

            if(setpgid(0,0) < 0){//把子进程放到一个新的进程组中，该进程组ID与子进程PID相同，这就确保了只有一个shell进程
                printf("setpgid error");
                exit(0);
            }

            if(execve(argv[0],argv,environ) < 0){
            //在子进程中调用execve寻找路径下的可执行文件，并判断是否可以找到
            printf("%s: Command not found\n",argv[0]);
            exit(0);
            //test14中的第一个错误，直接在这输出提示信息
            }
                       
        }

        sigprocmask(SIG_BLOCK,&mask_all,NULL); //恢复信号集合
        addjob(jobs,pid,bg==1 ? BG : FG,cmdline); //将子任务添加到任务列表中
        sigprocmask(SIG_SETMASK,&prev_one,NULL);//解除子进程对SIGCHLD的阻塞

        //父进程等待前台任务结束
        if(!bg){//如果不是后台进程，就等待当前的前台进程执行
            waitfg(pid);
        }
        else{//是后台进程，就在后台工作
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
    if(!strcmp(argv[0],"quit")){//如果是内置命令quit
        exit(0);//结束当前进程
    }
    if(!strcmp(argv[0],"jobs")){
        listjobs(jobs);
        return 1;
    }
    if(!strcmp(argv[0],"&")){
        return 1;//返回1，因为一个命令以&结尾，shell就在后台运行
    }
    if(!strcmp(argv[0],"bg")){//实现bg内置指令
        do_bgfg(argv);
        return 1;
    }
    if(!strcmp(argv[0],"fg")){//实现fg内置指令
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
 
    pid_t pid;                      /* 进程id */
    int jid;                        /* job的id */
    struct job_t * job;
    if (argv[1] == NULL){
        printf("%s command requires PID or %%jobid argument\n",argv[0]);
        return;
    }
    //第二个错误是没有传入pid或者jid（为空），就报错并返回
 
    if (argv[1][0] == '%'){   /* 如果输入的是jid（作业） */       
   //第三个错误命令是传入了，但是传入的数据不是不符合pid或jid的规范（输入必须为数字）
   //在这里判断并输出错误信息：fg: argument must be a PID or %%jobid\n
        jid = atoi(argv[1]+1);
        job = getjobjid(jobs,jid);//通过jid找到需要执行的job
        if(job == NULL){
            printf("%%%d: No such job\n",jid);
            return;
        }
   //第四个错误就是通过jid找到的job==null，因此“NO such job”
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
    {                              /* 给的是pid */
        pid = atoi(argv[1]);
        job = getjobpid(jobs,pid);
        if(job == NULL){
            printf("(%d): No such process\n",pid);
            return;
        }
//第五个错误就是通过jid找到的job==null，因此“NO such job”
        jid = job->jid;
    }
    if(pid > 0){
        if(!strcmp(argv[0],"bg")){          /* bg内置指令 */
            printf("[%d] (%d) %s",jid,pid,job->cmdline);
            job->state = BG;                /* 更改状态 */
            kill(-pid,SIGCONT);              /* 传递SIGCONT信号给进程组中的所有进程 */
            
        }else
        if(!strcmp(argv[0],"fg")){          /* fg内置指令 */
            job->state = FG;                /* 更改状态 */
            kill(-pid,SIGCONT);             /* 传递SIGCONT信号给进程组中的所有进程 */
            waitfg(pid);                    /* 等待前台job完成 */
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
    唯一的前台作业结束后，被sigchild_handler回收，deletejob之后,jobs列表里就没有前台作业了
    不断循环fgpid
    */
    while(pid == fgpid(jobs)){
        sleep(0);
        //注意sleep(0)并不是睡0秒，而是放弃时间片
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

    sigfillset(&mask_all); //保存当前堵塞的信号集合到mask_all中
    while((pid=waitpid(-1,&status,WNOHANG | WUNTRACED)) > 0){//WNOHANG为非阻塞的，只有不被阻塞的才进行deletejob
        if(WIFEXITED(status)){
            sigprocmask(SIG_BLOCK,&mask_all,&prev_all);//恢复信号集合，删除暂时阻塞
            deletejob(jobs,pid);//释放掉这个僵尸进程
            sigprocmask(SIG_SETMASK,&prev_all,NULL);//删除之后就接触子进程对于SIGCHLD的阻塞
        }
        //子进程是因为一个未被捕获的信号终止的(SIGINT)
        if(WIFSIGNALED(status)){
            int jid = pid2jid(pid);
            printf("Job [%d] (%d) terminated by signal %d\n",jid,pid,WTERMSIG(status));
            deletejob(jobs,pid);//终止就删除pid的job
        }
        //引起返回的子进程当前是停止的(SIGTSTP)
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
    pid_t pid = fgpid(jobs); //获取前台进程id
    if(pid > 0){
        kill(-pid,sig);//转发信号sig给进程组|pid|中的每个进程
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
    pid_t pid = fgpid(jobs);//获取前台进程id
    if(pid > 0){
        kill(-pid,sig);//转发信号sig给进程组|pid|中的每个进程
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




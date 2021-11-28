
#define THREAD_STACK_SIZE                  1024*64

// CPU thread descriptor
typedef struct __threaddesc
{
	int threadid;
	char *threadstack;
	void *threadfunc;
	ucontext_t threadcontext;
} threaddesc;

// IO thread descriptor
typedef struct __iodesc
{
	int fd;
	char *fileName;
	// char *ioStack;
	// void *ioFunc;
	bool openFlag;
	bool writeFlag;
	bool readFlag;
	bool closeFlag;
	char *buff;
	int buffSize;
	ucontext_t ioContext;
	bool ifDone;
} iodesc;
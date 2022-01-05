# Simple User-Level Thread Scheduler
**To use this implemented simple threads scheduler, you need to download all the head files and .c files**

**sut_init()**

Initializes the SUT library. Needs to be called before making any other API calls.

**sut_create(sut_task_f fn)**

Creates the task. The main body of the task is the function that is passed as the only argument to this function. On success this returns a True (1) else returns False (0).

**sut_yield()**

The C-EXEC will take over the control. That is the user task’s context is saved in a task control block (TCB) and we load the context of C-EXEC and start it. We also put the task in the back of the task ready queue.

**sut_exit()**

The C-EXEC will take over the control like the above case. The major difference is that TCB is not updated or the task inserted back into the task ready queue.

**sut_open(char \*dest)**

The C-EXEC will take over the control. We save the user task’s context in a task control block (TCB) and we load the context of C-EXEC and start it, which would pick the next user task to run. We put the current task in the back of the wait queue. The I-EXEC would execute the function that would actually open the file and the result of the open would be returned by the sut_open() function. The result of the open is an integer much like the file descriptor returned by the OS. 

**sut_write(int fd, char \*buf, int size)**

The C-EXEC will take over the control like the above calls. The I-EXEC thread is responsible for writing the data to the file. It uses the file descriptor (fd) to select the file that needs to receive the data. The contents of the memory buffer passed into the sut_write() is emptied into the corresponding file.

**sut_close(int fd)**

The C-EXEC will take over the control like the above calls. The I-EXEC thread is responsible for closing the file. We need not have done file closing in an asynchronous manner – so it is done to keep all IO calls asynchronous.

**sut_read(int fd, char \*buf, int size)**

The C-EXEC will take over the control. We save the user task’s context in a task control block (TCB) and we load the context of C-EXEC and start it, which would pick the next user task to run. We put the current task in the back of the wait queue. The I-EXEC thread is responsible for reading the data from the file. Once the data is completely read and available in a memory area that is passed into the function by the calling task. That is, the I-EXEC does not allocate memory – it uses a memory buffer given to it. Assume the memory buffer provided by the calling task is big enough for the data read from the disk.

**sut_shutdown()**

Empty the queues and shut down the whole program.

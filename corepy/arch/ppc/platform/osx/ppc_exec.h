/* Copyright (c) 2006-2008 The Trustees of Indiana University.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * - Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * 
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * 
 * - Neither the Indiana University nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

// Native code for executing instruction streams on OS X

#ifndef PPC_EXEC_H
#define PPC_EXEC_H

#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>

// Fix bug in carbon headers
// http://aspn.activestate.com/ASPN/Mail/Message/wxPython-users/1808182

#ifndef CELL
#define scalb scalbn 

#include <CoreServices/CoreServices.h>
#include <pthread.h>
#include <mach/thread_act.h>
#include <signal.h>

#endif


// ------------------------------------------------------------
// Typedefs
// ------------------------------------------------------------

// Parameter passing structures
struct ExecParams {
  unsigned long p1;
  unsigned long p2;
  unsigned long p3;
  unsigned long p4;
  unsigned long p5;
  unsigned long p6;
  unsigned long p7;
  unsigned long p8;
};


struct ThreadParams {
  long addr;
  struct ExecParams params;
  union {
    long l;
    double d;
  } ret;
};


//Returned to python in async mode
struct ThreadInfo {
  pthread_t th;
  int mode;
};


// Function pointers for different return values
typedef long (*Stream_func_int)(struct ExecParams);
typedef double (*Stream_func_fp)(struct ExecParams);


// ------------------------------------------------------------
// Code
// ------------------------------------------------------------

// ------------------------------------------------------------
// Function: make_executable
// Arguments:
//   addr - the address of the code stream
//   size - the size of the code stream in bytes
// Return: void
// Description:
//   Make an instruction stream executable.  The stream must 
//   be a contiguous sequence of bytes, forming instructions.
// ------------------------------------------------------------

int make_executable(long addr, long size) {
  // sys_icache_invalidate may be useful in the future for cache control. 
  // It will remove the dependency on carbon.  It's new to Leopard, so not
  // useful yet...
  // sys_icache_invalidate((char *)addr, size * 4);
#ifdef __MACH__
#ifdef __powerpc__
  MakeDataExecutable((void *)addr, size * 4);
#endif
#endif
  return 0;
}


// ------------------------------------------------------------
// Function: cancel_async
// Arguments:
//   t - thread id cancel
// Return: 0 on success, -1 on failure
// Description:
//   The native interface for cancelling execution of a thread.
// ------------------------------------------------------------

int cancel_async(struct ThreadInfo* tinfo) {
  return pthread_cancel(tinfo->th);
}


// ------------------------------------------------------------
// Function: suspend_async
// Arguments:
//   t - thread id to cancle
// Return: 0 on success, -1 on failure
// Description:
//   The native interface for suspending execution of a thread.
//   Because there is no pthread interface for thread 
//   susped/resume, this suspends the underlying OS X mach 
//   thread.
// ------------------------------------------------------------

int suspend_async(struct ThreadInfo* tinfo) {
  // ref: http://gcc.gnu.org/ml/java/2001-12/msg00404.html
  mach_port_t mthread = pthread_mach_thread_np(tinfo->th);
  if(thread_suspend(mthread) != KERN_SUCCESS) {
    return -1;
  }

  return 0;
}


// ------------------------------------------------------------
// Function: resume_async
// Arguments:
//   t - thread id to resume
// Return: 0 on success, -1 on failure
// Description:
//   The native interface for resuming execution of a thread.
//   Because there is no pthread interface for thread 
//   susped/resume, this suspends the underlying OS X mach 
//   thread.
// ------------------------------------------------------------

int resume_async(struct ThreadInfo* tinfo) {
  mach_port_t mthread = pthread_mach_thread_np(tinfo->th);
  if(thread_resume(mthread) != KERN_SUCCESS) {
    return -1;
  }

  return 0;
}


// ------------------------------------------------------------
// Function: run_stream
// Arguments:
//   *addr - pointer to thread data 
// Return: undefined
// Description:
//   The thread execution function.  The code stream is passed
//   as the argument via pthreads and executed here.
// ------------------------------------------------------------

void cleanup(void* params) {
    struct ThreadParams *p = (struct ThreadParams*)params;
    free(p);
}


void *run_stream_int(void *params) {
  struct ThreadParams *p = (struct ThreadParams *)params;
  pthread_cleanup_push(cleanup, params);

  p->ret.l = ((Stream_func_int)p->addr)(p->params);

  pthread_cleanup_pop(0);
  return params;
}


void *run_stream_fp(void *params) {
  struct ThreadParams *p = (struct ThreadParams *)params;
  pthread_cleanup_push(cleanup, params);

  p->ret.d = ((Stream_func_fp)p->addr)(p->params);

  pthread_cleanup_pop(0);
  return params;
}


// ------------------------------------------------------------
// Function: execute_{int, fp}_async
// Arguments:
//   addr   - address of the instruction stream
//   params - parameters to pass to the instruction stream
// Return: a new thread id
// Description:
//   The native interface for executing a code stream as a 
//   thread.  make_executable must be called first.
// ------------------------------------------------------------


struct ThreadInfo* execute_int_async(long addr, struct ExecParams params) {
  int rc;

  struct ThreadInfo* tinfo = malloc(sizeof(struct ThreadInfo));
  struct ThreadParams* tparams = malloc(sizeof(struct ThreadParams));
  tparams->addr = addr;
  tparams->params = params;

  rc = pthread_create(&tinfo->th, NULL, run_stream_int, (void *)tparams);
  if(rc) {
    printf("Error creating async stream: %d\n", rc);
  }

  return tinfo;
}


struct ThreadInfo* execute_fp_async(long addr, struct ExecParams params) {
  int rc;

  struct ThreadInfo* tinfo = malloc(sizeof(struct ThreadInfo));
  struct ThreadParams* tparams = malloc(sizeof(struct ThreadParams));
  tparams->addr = addr;
  tparams->params = params;

  rc = pthread_create(&tinfo->th, NULL, run_stream_fp, (void *)tparams);
  if(rc) {
    printf("Error creating async stream: %d\n", rc);
  }

  return tinfo;
}


long join_int(struct ThreadInfo* tinfo) {
  struct ThreadParams *p;

  pthread_join(tinfo->th, (void**)&p);

  long result = p->ret.l;

  free(tinfo);
  free(p);
  return result;
}


double join_fp(struct ThreadInfo* tinfo) {
  struct ThreadParams *p;

  pthread_join(tinfo->th, (void**)&p);

  double result = p->ret.d;

  free(tinfo);
  free(p);
  return result;
}


long execute_int(long addr, struct ExecParams params) {
  return ((Stream_func_int)addr)(params);
}


double execute_fp(long addr, struct ExecParams params) {
  return ((Stream_func_fp)addr)(params);
}

#endif // PPC_EXEC_H

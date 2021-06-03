/****************************************************************************
 * arch/misoc/src/minerva/minerva_initialstate.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/tls.h>
#include <arch/irq.h>
#include <arch/minerva/csrdefs.h>
#include <arch/minerva/irq.h>

#include "minerva.h"
#include "chip.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_initial_state
 *
 * Description:
 *   A new thread is being started and a new TCB
 *   has been created. This function is called to initialize
 *   the processor specific portions of the new TCB.
 *
 *   This function must setup the initial architecture registers
 *   and/or  stack so that execution will begin at tcb->start
 *   on the next context switch.
 *
 ****************************************************************************/

void up_initial_state(struct tcb_s *tcb)
{
  uint32_t regval;
  struct xcptcontext *xcp = &tcb->xcp;

  /* Initialize the idle thread stack */

  if (tcb->pid == 0)
    {
      tcb->stack_alloc_ptr = (void *)(g_idle_topstack -
                                      CONFIG_IDLETHREAD_STACKSIZE);
      tcb->stack_base_ptr  = tcb->stack_alloc_ptr;
      tcb->adj_stack_size  = CONFIG_IDLETHREAD_STACKSIZE -
                             sizeof(struct task_info_s);
    }

  /* Initialize the initial exception register context structure */

  memset(xcp, 0, sizeof(struct xcptcontext));

  /* Save the initial stack pointer.  Hmmm.. the stack is set to the very
   * beginning of the stack region.  Some functions may want to store data on
   * the caller's stack and it might be good to reserve some space.  However,
   * only the start function would do that and we have control over that one.
   */

  xcp->regs[REG_SP] = (uint32_t)tcb->stack_base_ptr +
                                tcb->adj_stack_size;

  /* Save the task entry point */

  xcp->regs[REG_CSR_MEPC] = (uint32_t)tcb->start;

  xcp->regs[REG_CSR_MSTATUS] = CSR_MSTATUS_MPIE;

  /* If this task is running PIC, then set the PIC base register to the
   * address of the allocated D-Space region.
   */

#ifdef CONFIG_PIC
#  warning "Missing logic"
#endif

  /* Set privileged- or unprivileged-mode, depending on how NuttX is
   * configured and what kind of thread is being started. If the kernel
   * build is not selected, then all threads run in privileged thread mode.
   */

#ifdef CONFIG_BUILD_KERNEL
#  warning "Missing logic"
#endif
}

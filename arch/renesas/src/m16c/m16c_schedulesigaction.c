/****************************************************************************
 * arch/renesas/src/m16c/m16c_schedulesigaction.c
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

#include <stdint.h>
#include <sched.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>

#include "sched/sched.h"
#include "renesas_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_schedule_sigaction
 *
 * Description:
 *   This function is called by the OS when one or more
 *   signal handling actions have been queued for execution.
 *   The architecture specific code must configure things so
 *   that the 'sigdeliver' callback is executed on the thread
 *   specified by 'tcb' as soon as possible.
 *
 *   This function may be called from interrupt handling logic.
 *
 *   This operation should not cause the task to be unblocked
 *   nor should it cause any immediate execution of sigdeliver.
 *   Typically, a few cases need to be considered:
 *
 *   (1) This function may be called from an interrupt handler
 *       During interrupt processing, all xcptcontext structures
 *       should be valid for all tasks.  That structure should
 *       be modified to invoke sigdeliver() either on return
 *       from (this) interrupt or on some subsequent context
 *       switch to the recipient task.
 *   (2) If not in an interrupt handler and the tcb is NOT
 *       the currently executing task, then again just modify
 *       the saved xcptcontext structure for the recipient
 *       task so it will invoke sigdeliver when that task is
 *       later resumed.
 *   (3) If not in an interrupt handler and the tcb IS the
 *       currently executing task -- just call the signal
 *       handler now.
 *
 * Assumptions:
 *   Called from critical section
 *
 ****************************************************************************/

void up_schedule_sigaction(struct tcb_s *tcb, sig_deliver_t sigdeliver)
{
  sinfo("tcb=%p sigdeliver=%p\n", tcb, sigdeliver);

  /* Refuse to handle nested signal actions */

  if (!tcb->xcp.sigdeliver)
    {
      tcb->xcp.sigdeliver = sigdeliver;

      /* First, handle some special cases when the signal is
       * being delivered to the currently executing task.
       */

      sinfo("rtcb=%p g_current_regs=%p\n",
            this_task_inirq(), g_current_regs);

      if (tcb == this_task_inirq())
        {
          /* CASE 1:  We are not in an interrupt handler and
           * a task is signalling itself for some reason.
           */

          if (!g_current_regs)
            {
              /* In this case just deliver the signal now. */

              sigdeliver(tcb);
              tcb->xcp.sigdeliver = NULL;
            }

          /* CASE 2:  We are in an interrupt handler AND the
           * interrupted task is the same as the one that
           * must receive the signal, then we will have to modify
           * the return state as well as the state in the TCB.
           */

          else
            {
              /* Save the return PC and SR and one scratch register
               * These will be restored by the signal trampoline after
               * the signals have been delivered.
               */

              tcb->xcp.saved_pc[0]   = g_current_regs[REG_PC];
              tcb->xcp.saved_pc[1]   = g_current_regs[REG_PC + 1];
              tcb->xcp.saved_flg     = g_current_regs[REG_FLG];

              /* Then set up to vector to the trampoline with interrupts
               * disabled
               */

              g_current_regs[REG_PC] = (uint32_t)renesas_sigdeliver >> 8;
              g_current_regs[REG_PC + 1] = (uint32_t)renesas_sigdeliver;
              g_current_regs[REG_FLG] &= ~M16C_FLG_I;

              /* And make sure that the saved context in the TCB
               * is the same as the interrupt return context.
               */

              renesas_copystate(tcb->xcp.regs, g_current_regs);
            }
        }

      /* Otherwise, we are (1) signaling a task is not running
       * from an interrupt handler or (2) we are not in an
       * interrupt handler and the running task is signalling
       * some non-running task.
       */

      else
        {
          /* Save the return PC and SR and one scratch register
           * These will be restored by the signal trampoline after
           * the signals have been delivered.
           */

          tcb->xcp.saved_pc[0]  = tcb->xcp.regs[REG_PC];
          tcb->xcp.saved_pc[1]  = tcb->xcp.regs[REG_PC + 1];
          tcb->xcp.saved_flg    = tcb->xcp.regs[REG_FLG];

          /* Then set up to vector to the trampoline with interrupts
           * disabled
           */

          tcb->xcp.regs[REG_PC] = (uint32_t)renesas_sigdeliver >> 8;
          tcb->xcp.regs[REG_PC + 1] = (uint32_t)renesas_sigdeliver;
          tcb->xcp.regs[REG_FLG] &= ~M16C_FLG_I;
        }
    }
}

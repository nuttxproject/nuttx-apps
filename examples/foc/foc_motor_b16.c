/****************************************************************************
 * apps/examples/foc/foc_motor_b16.c
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

#include <assert.h>

#include "foc_motor_b16.h"

#include "foc_cfg.h"
#include "foc_adc.h"
#include "foc_debug.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Type Definition
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: foc_runmode_init
 ****************************************************************************/

static int foc_runmode_init(FAR struct foc_motor_b16_s *motor)
{
  int ret = OK;

  switch (motor->envp->fmode)
    {
      case FOC_FMODE_IDLE:
        {
          motor->foc_mode = FOC_HANDLER_MODE_IDLE;
          break;
        }

      case FOC_FMODE_VOLTAGE:
        {
          motor->foc_mode = FOC_HANDLER_MODE_VOLTAGE;
          break;
        }

      case FOC_FMODE_CURRENT:
        {
          motor->foc_mode = FOC_HANDLER_MODE_CURRENT;
          break;
        }

      default:
        {
          PRINTF("ERROR: unsupported op mode %d\n", motor->envp->fmode);
          ret = -EINVAL;
          goto errout;
        }
    }

  /* Force open-loop if sensorlesss */

#ifdef CONFIG_EXAMPLES_FOC_SENSORLESS
#  ifdef CONFIG_EXAMPLES_FOC_HAVE_OPENLOOP
  motor->openloop_now = true;
#  else
#    error
#  endif
#endif

errout:
  return ret;
}

/****************************************************************************
 * Name: foc_motor_configure
 ****************************************************************************/

static int foc_motor_configure(FAR struct foc_motor_b16_s *motor)
{
#ifdef CONFIG_INDUSTRY_FOC_CONTROL_PI
  struct foc_initdata_b16_s ctrl_cfg;
#endif
#ifdef CONFIG_INDUSTRY_FOC_MODULATION_SVM3
  struct foc_mod_cfg_b16_s mod_cfg;
#endif
#ifdef CONFIG_EXAMPLES_FOC_STATE_USE_MODEL_PMSM
  struct foc_model_pmsm_cfg_b16_s pmsm_cfg;
#endif
  int              ret  = OK;

  DEBUGASSERT(motor);

#ifdef CONFIG_EXAMPLES_FOC_HAVE_VEL
  /* Initialize velocity ramp */

  ret = foc_ramp_init_b16(&motor->ramp,
                          motor->per,
                          ftob16(RAMP_CFG_THR),
                          ftob16(RAMP_CFG_ACC),
                          ftob16(RAMP_CFG_ACC));
  if (ret < 0)
    {
      PRINTF("ERROR: foc_ramp_init failed %d\n", ret);
      goto errout;
    }
#endif

  /* Initialize FOC handler */

  ret = foc_handler_init_b16(&motor->handler,
                             &g_foc_control_pi_b16,
                             &g_foc_mod_svm3_b16);
  if (ret < 0)
    {
      PRINTF("ERROR: foc_handler_init failed %d\n", ret);
      goto errout;
    }

#ifdef CONFIG_INDUSTRY_FOC_CONTROL_PI
  /* Get PI controller configuration */

  ctrl_cfg.id_kp = ftob16(motor->envp->pi_kp / 1000.0f);
  ctrl_cfg.id_ki = ftob16(motor->envp->pi_ki / 1000.0f);
  ctrl_cfg.iq_kp = ftob16(motor->envp->pi_kp / 1000.0f);
  ctrl_cfg.iq_ki = ftob16(motor->envp->pi_ki / 1000.0f);
#endif

#ifdef CONFIG_INDUSTRY_FOC_MODULATION_SVM3
  /* Get SVM3 modulation configuration */

  mod_cfg.pwm_duty_max = motor->pwm_duty_max;
#endif

  /* Configure FOC handler */

  foc_handler_cfg_b16(&motor->handler, &ctrl_cfg, &mod_cfg);

#ifdef CONFIG_EXAMPLES_FOC_STATE_USE_MODEL_PMSM
  /* Initialize PMSM model */

  ret = foc_model_init_b16(&motor->model,
                           &g_foc_model_pmsm_ops_b16);
  if (ret < 0)
    {
      PRINTF("ERROR: foc_model_init failed %d\n", ret);
      goto errout;
    }

  /* Get PMSM model configuration */

  pmsm_cfg.poles      = FOC_MODEL_POLES;
  pmsm_cfg.res        = ftob16(FOC_MODEL_RES);
  pmsm_cfg.ind        = ftob16(FOC_MODEL_IND);
  pmsm_cfg.iner       = ftob16(FOC_MODEL_INER);
  pmsm_cfg.flux_link  = ftob16(FOC_MODEL_FLUX);
  pmsm_cfg.ind_d      = ftob16(FOC_MODEL_INDD);
  pmsm_cfg.ind_q      = ftob16(FOC_MODEL_INDQ);
  pmsm_cfg.per        = motor->per;
  pmsm_cfg.iphase_adc = motor->iphase_adc;

  /* Configure PMSM model */

  foc_model_cfg_b16(&motor->model, &pmsm_cfg);
#endif

errout:
  return ret;
}

/****************************************************************************
 * Name: foc_motor_vbus
 ****************************************************************************/

static int foc_motor_vbus(FAR struct foc_motor_b16_s *motor, uint32_t vbus)
{
  DEBUGASSERT(motor);

  /* Update motor VBUS */

  motor->vbus = b16muli(vbus, ftob16(VBUS_ADC_SCALE));

  return OK;
}

#ifdef CONFIG_EXAMPLES_FOC_HAVE_TORQ
/****************************************************************************
 * Name: foc_motor_torq
 ****************************************************************************/

static int foc_motor_torq(FAR struct foc_motor_b16_s *motor, uint32_t torq)
{
  b16_t tmp1 = 0;
  b16_t tmp2 = 0;

  DEBUGASSERT(motor);

  /* Update motor torqocity destination
   * NOTE: torqmax may not fit in b16_t so we can't use b16idiv()
   */

  tmp1 = itob16(motor->envp->torqmax / 1000);
  tmp2 = b16mulb16(ftob16(SETPOINT_ADC_SCALE), tmp1);

  motor->torq.des = b16muli(tmp2, torq);

  return OK;
}
#endif

#ifdef CONFIG_EXAMPLES_FOC_HAVE_VEL
/****************************************************************************
 * Name: foc_motor_vel
 ****************************************************************************/

static int foc_motor_vel(FAR struct foc_motor_b16_s *motor, uint32_t vel)
{
  b16_t tmp1 = 0;
  b16_t tmp2 = 0;

  DEBUGASSERT(motor);

  /* Update motor velocity destination
   * NOTE: velmax may not fit in b16_t so we can't use b16idiv()
   */

  tmp1 = itob16(motor->envp->velmax / 1000);
  tmp2 = b16mulb16(ftob16(SETPOINT_ADC_SCALE), tmp1);

  motor->vel.des = b16muli(tmp2, vel);

  return OK;
}
#endif

#ifdef CONFIG_EXAMPLES_FOC_HAVE_POS
/****************************************************************************
 * Name: foc_motor_pos
 ****************************************************************************/

static int foc_motor_pos(FAR struct foc_motor_b16_s *motor, uint32_t pos)
{
  b16_t tmp1 = 0;
  b16_t tmp2 = 0;

  DEBUGASSERT(motor);

  /* Update motor posocity destination
   * NOTE: posmax may not fit in b16_t so we can't use b16idiv()
   */

  tmp1 = itob16(motor->envp->posmax / 1000);
  tmp2 = b16mulb16(ftob16(SETPOINT_ADC_SCALE), tmp1);

  motor->pos.des = b16muli(tmp2, pos);

  return OK;
}
#endif

/****************************************************************************
 * Name: foc_motor_setpoint
 ****************************************************************************/

static int foc_motor_setpoint(FAR struct foc_motor_b16_s *motor, uint32_t sp)
{
  int ret = OK;

  switch (motor->envp->mmode)
    {
#ifdef CONFIG_EXAMPLES_FOC_HAVE_TORQ
      case FOC_MMODE_TORQ:
        {
          /* Update motor torque destination */

          ret = foc_motor_torq(motor, sp);
          if (ret < 0)
            {
              PRINTF("ERROR: foc_motor_torq failed %d!\n", ret);
              goto errout;
            }

          break;
        }
#endif

#ifdef CONFIG_EXAMPLES_FOC_HAVE_VEL
      case FOC_MMODE_VEL:
        {
          /* Update motor velocity destination */

          ret = foc_motor_vel(motor, sp);
          if (ret < 0)
            {
              PRINTF("ERROR: foc_motor_vel failed %d!\n", ret);
              goto errout;
            }

          break;
        }
#endif

#ifdef CONFIG_EXAMPLES_FOC_HAVE_POS
      case FOC_MMODE_POS:
        {
          /* Update motor position destination */

          ret = foc_motor_pos(motor, sp);
          if (ret < 0)
            {
              PRINTF("ERROR: foc_motor_pos failed %d!\n", ret);
              goto errout;
            }

          break;
        }
#endif

      default:
        {
          PRINTF("ERROR: unsupported ctrl mode %d\n", motor->envp->mmode);
          ret = -EINVAL;
          goto errout;
        }
    }

errout:
  return ret;
}

/****************************************************************************
 * Name: foc_motor_state
 ****************************************************************************/

static int foc_motor_state(FAR struct foc_motor_b16_s *motor, int state)
{
  int ret = OK;

  DEBUGASSERT(motor);

  /* Update motor state */

  switch (state)
    {
      case FOC_EXAMPLE_STATE_FREE:
        {
          motor->dir = DIR_NONE_B16;

          /* Force DQ vector to zero */

          motor->dq_ref.q = 0;
          motor->dq_ref.d = 0;

          break;
        }

      case FOC_EXAMPLE_STATE_STOP:
        {
          motor->dir = DIR_NONE_B16;

          /* DQ vector not zero - active brake */

          break;
        }

      case FOC_EXAMPLE_STATE_CW:
        {
          motor->dir = DIR_CW_B16;

          break;
        }

      case FOC_EXAMPLE_STATE_CCW:
        {
          motor->dir = DIR_CCW_B16;

          break;
        }

      default:
        {
          ret = -EINVAL;
          goto errout;
        }
    }

  /* Reset current setpoint */

#ifdef CONFIG_EXAMPLES_FOC_HAVE_TORQ
  motor->torq.set = 0;
#endif
#ifdef CONFIG_EXAMPLES_FOC_HAVE_VEL
  motor->vel.set = 0;
#endif
#ifdef CONFIG_EXAMPLES_FOC_HAVE_POS
  motor->pos.set = 0;
#endif

errout:
  return ret;
}

/****************************************************************************
 * Name: foc_motor_start
 ****************************************************************************/

static int foc_motor_start(FAR struct foc_motor_b16_s *motor, bool start)
{
  int ret = OK;

  DEBUGASSERT(motor);

  if (start == true)
    {
      /* Start motor if VBUS data present */

      if (motor->mq.vbus > 0)
        {
          /* Configure motor controller */

          PRINTF("Configure motor %d!\n", motor->envp->id);

          ret = foc_motor_configure(motor);
          if (ret < 0)
            {
              PRINTF("ERROR: foc_motor_configure failed %d!\n", ret);
              goto errout;
            }

          /* Start/stop FOC dev request */

          motor->startstop = true;
        }
    }
  else
    {
      /* Start/stop FOC dev request */

      motor->startstop = true;
    }

errout:
  return ret;
}

/****************************************************************************
 * Name: foc_motor_run
 ****************************************************************************/

static int foc_motor_run(FAR struct foc_motor_b16_s *motor)
{
  b16_t q_ref = 0;
  b16_t d_ref = 0;
  int   ret   = OK;

  DEBUGASSERT(motor);

#ifdef CONFIG_EXAMPLES_FOC_HAVE_OPENLOOP
#  ifdef CONFIG_EXAMPLES_FOC_HAVE_VEL
  /* Open-loop works only in velocity control mode */

  if (motor->openloop_now == true)
    {
      if (motor->envp->mmode != FOC_MMODE_VEL)
        {
          PRINTF("ERROR: open-loop only with FOC_MMODE_VEL\n");
          ret = -EINVAL;
          goto errout;
        }
    }
#  else
#    error
#  endif
#endif

  /* Get previous DQ */

  q_ref = motor->dq_ref.q;
  d_ref = motor->dq_ref.d;

  /* Controller */

  switch (motor->envp->mmode)
    {
#ifdef CONFIG_EXAMPLES_FOC_HAVE_TORQ
      case FOC_MMODE_TORQ:
        {
          motor->torq.set = b16mulb16(motor->dir, motor->torq.des);

          q_ref = motor->torq.set;
          d_ref = 0;

          break;
        }
#endif

#ifdef CONFIG_EXAMPLES_FOC_HAVE_VEL
      case FOC_MMODE_VEL:
        {
          /* Run velocity ramp controller */

          ret = foc_ramp_run_b16(&motor->ramp, motor->vel.des,
                                 motor->vel.now, &motor->vel.set);
          if (ret < 0)
            {
              PRINTF("ERROR: foc_ramp_run failed %d\n", ret);
              goto errout;
            }

          break;
        }
#endif

      default:
        {
          ret = -EINVAL;
          goto errout;
        }
    }

#ifdef CONFIG_EXAMPLES_FOC_HAVE_OPENLOOP
  /* Force open-loop current */

  if (motor->openloop_now == true)
    {
      /* Get open-loop currents
       * NOTE: Id always set to 0
       */

      motor->dq_ref.q = b16idiv(motor->envp->qparam, 1000);
      motor->dq_ref.d = 0;
    }
#endif

  /* Set DQ reference frame */

  motor->dq_ref.q = q_ref;
  motor->dq_ref.d = d_ref;

  /* DQ compensation */

  motor->vdq_comp.q = 0;
  motor->vdq_comp.d = 0;

errout:
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: foc_motor_init
 ****************************************************************************/

int foc_motor_init(FAR struct foc_motor_b16_s *motor,
                   FAR struct foc_ctrl_env_s *envp)
{
#ifdef CONFIG_EXAMPLES_FOC_HAVE_OPENLOOP
  struct foc_openloop_cfg_b16_s ol_cfg;
#endif
  int                           ret = OK;

  DEBUGASSERT(motor);
  DEBUGASSERT(envp);

  /* Reset data */

  memset(motor, 0, sizeof(struct foc_motor_b16_s));

  /* Connect envp with motor handler */

  motor->envp = envp;

  /* Initialize motor data */

  motor->per        = b16divi(b16ONE, CONFIG_EXAMPLES_FOC_NOTIFIER_FREQ);
  motor->iphase_adc = ftob16((CONFIG_EXAMPLES_FOC_IPHASE_ADC) / 100000.0f);

#ifdef CONFIG_EXAMPLES_FOC_HAVE_OPENLOOP
  /* Initialize open-loop angle handler */

  foc_angle_init_b16(&motor->openloop,
                     &g_foc_angle_ol_b16);

  /* Configure open-loop angle handler */

  ol_cfg.per = motor->per;
  foc_angle_cfg_b16(&motor->openloop, &ol_cfg);
#endif

  /* Initialize controller state */

  motor->ctrl_state = FOC_CTRL_STATE_INIT;

  return ret;
}

/****************************************************************************
 * Name: foc_motor_deinit
 ****************************************************************************/

int foc_motor_deinit(FAR struct foc_motor_b16_s *motor)
{
  int ret = OK;

  DEBUGASSERT(motor);

#ifdef CONFIG_EXAMPLES_FOC_STATE_USE_MODEL_PMSM
  /* Deinitialize PMSM model */

  ret = foc_model_deinit_b16(&motor->model);
  if (ret < 0)
    {
      PRINTF("ERROR: foc_model_deinit failed %d\n", ret);
      goto errout;
    }
#endif

  /* Deinitialize FOC handler */

  ret = foc_handler_deinit_b16(&motor->handler);
  if (ret < 0)
    {
      PRINTF("ERROR: foc_handler_deinit failed %d\n", ret);
      goto errout;
    }

  /* Reset data */

  memset(motor, 0, sizeof(struct foc_motor_b16_s));

errout:
  return ret;
}

/****************************************************************************
 * Name: foc_motor_get
 ****************************************************************************/

int foc_motor_get(FAR struct foc_motor_b16_s *motor)
{
  struct foc_angle_in_b16_s  ain;
  struct foc_angle_out_b16_s aout;
  int                        ret = OK;

  DEBUGASSERT(motor);

  /* Update open-loop angle handler */

  ain.vel   = motor->vel.set;
  ain.angle = motor->angle_now;
  ain.dir   = motor->dir;

#ifdef CONFIG_EXAMPLES_FOC_HAVE_OPENLOOP
  foc_angle_run_b16(&motor->openloop, &ain, &aout);

  /* Store open-loop angle */

  motor->angle_ol = aout.angle;

  /* Get phase angle now */

  if (motor->openloop_now == true)
    {
      motor->angle_now = motor->angle_ol;
    }
  else
#endif
    {
      /* TODO: get phase angle from observer or sensor */

      ASSERT(0);
    }

#ifdef CONFIG_EXAMPLES_FOC_HAVE_OPENLOOP
  if (motor->openloop_now == true)
    {
      /* No velocity feedback - assume that velocity now is velocity set */

      motor->vel.now = motor->vel.set;
    }
  else
#endif
    {
      /* TODO: velocity observer or sensor */
    }

  return ret;
}

/****************************************************************************
 * Name: foc_motor_control
 ****************************************************************************/

int foc_motor_control(FAR struct foc_motor_b16_s *motor)
{
  int ret = OK;

  DEBUGASSERT(motor);

  /* Controller state machine */

  switch (motor->ctrl_state)
    {
      case FOC_CTRL_STATE_INIT:
        {
          /* Next state */

          motor->ctrl_state += 1;
          motor->foc_mode = FOC_HANDLER_MODE_IDLE;

          break;
        }

      case FOC_CTRL_STATE_RUN_INIT:
        {
          /* Initialize run controller mode */

          ret = foc_runmode_init(motor);
          if (ret < 0)
            {
              PRINTF("ERROR: foc_runmode_init failed %d!\n", ret);
              goto errout;
            }

          /* Next state */

          motor->ctrl_state += 1;
        }

      case FOC_CTRL_STATE_RUN:
        {
          /* Run motor */

          ret = foc_motor_run(motor);
          if (ret < 0)
            {
              PRINTF("ERROR: foc_motor_run failed %d!\n", ret);
              goto errout;
            }

          break;
        }

      case FOC_CTRL_STATE_IDLE:
        {
          motor->foc_mode = FOC_HANDLER_MODE_IDLE;

          break;
        }

      default:
        {
          PRINTF("ERROR: invalid ctrl_state=%d\n", motor->ctrl_state);
          ret = -EINVAL;
          goto errout;
        }
    }

errout:
  return ret;
}

/****************************************************************************
 * Name: foc_motor_handle
 ****************************************************************************/

int foc_motor_handle(FAR struct foc_motor_b16_s *motor,
                     FAR struct foc_mq_s *handle)
{
  int ret = OK;

  DEBUGASSERT(motor);
  DEBUGASSERT(handle);

  /* Terminate */

  if (handle->quit == true)
    {
      motor->mq.quit  = true;
    }

  /* Update motor VBUS */

  if (motor->mq.vbus != handle->vbus)
    {
      PRINTFV("Set vbus=%" PRIu32 " for FOC driver %d!\n",
              handle->vbus, motor->envp->id);

      ret = foc_motor_vbus(motor, handle->vbus);
      if (ret < 0)
        {
          PRINTF("ERROR: foc_motor_vbus failed %d!\n", ret);
          goto errout;
        }

      motor->mq.vbus = handle->vbus;
    }

  /* Update motor velocity destination */

  if (motor->mq.setpoint != handle->setpoint)
    {
      PRINTFV("Set setpoint=%" PRIu32 " for FOC driver %d!\n",
              handle->setpoint, motor->envp->id);

      /* Update motor setpoint */

      ret = foc_motor_setpoint(motor, handle->setpoint);
      if (ret < 0)
        {
          PRINTF("ERROR: foc_motor_setpoint failed %d!\n", ret);
          goto errout;
        }

      motor->mq.setpoint = handle->setpoint;
    }

  /* Update motor state */

  if (motor->mq.app_state != handle->app_state)
    {
      PRINTFV("Set app_state=%d for FOC driver %d!\n",
              handle->app_state, motor->envp->id);

      ret = foc_motor_state(motor, handle->app_state);
      if (ret < 0)
        {
          PRINTF("ERROR: foc_motor_state failed %d!\n", ret);
          goto errout;
        }

      motor->mq.app_state = handle->app_state;
    }

  /* Start/stop controller */

  if (motor->mq.start != handle->start)
    {
      PRINTFV("Set start=%d for FOC driver %d!\n",
              handle->start, motor->envp->id);

      /* Start/stop motor controller */

      ret = foc_motor_start(motor, handle->start);
      if (ret < 0)
        {
          PRINTF("ERROR: foc_motor_start failed %d!\n", ret);
          goto errout;
        }

      motor->mq.start = handle->start;
    }

errout:
  return ret;
}
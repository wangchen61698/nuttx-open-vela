/****************************************************************************
 * drivers/power/battery_gauge.c
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

/* Upper-half, character driver for battery fuel gauge. */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <debug.h>
#include <poll.h>
#include <fcntl.h>

#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/power/battery_gauge.h>
#include <nuttx/power/battery_ioctl.h>

/* This driver requires:
 *
 * CONFIG_BATTERY_GAUGE - Upper half battery driver support
 */

#if defined(CONFIG_BATTERY_GAUGE)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private type
 ****************************************************************************/

struct battery_gauge_priv_s
{
  struct list_node  node;
  sem_t             lock;
  sem_t             wait;
  uint32_t          mask;
  FAR struct pollfd *fds;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Character driver methods */

static int     bat_gauge_open(FAR struct file *filep);
static int     bat_gauge_close(FAR struct file *filep);
static ssize_t bat_gauge_read(FAR struct file *filep,
                              FAR char *buffer,
                              size_t buflen);
static ssize_t bat_gauge_write(FAR struct file *filep,
                               FAR const char *buffer,
                               size_t buflen);
static int     bat_gauge_ioctl(FAR struct file *filep, int cmd,
                               unsigned long arg);
static int     bat_gauge_poll(FAR struct file *filep,
                               FAR struct pollfd *fds, bool setup);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_batteryops =
{
  bat_gauge_open,
  bat_gauge_close,
  bat_gauge_read,
  bat_gauge_write,
  NULL,
  bat_gauge_ioctl,
  bat_gauge_poll
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int battery_gauge_notify(FAR struct battery_gauge_priv_s *priv,
                                   uint32_t mask)
{
  FAR struct pollfd *fd = priv->fds;
  int semcnt;
  int ret;

  ret = nxsem_wait_uninterruptible(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  priv->mask |= mask;
  if (priv->mask)
    {
      fd->revents |= POLLIN;
      nxsem_get_value(fd->sem, &semcnt);
      if (semcnt < 1)
        {
          nxsem_post(fd->sem);
        }

      nxsem_get_value(&priv->wait, &semcnt);
      if (semcnt < 1)
        {
          nxsem_post(&priv->wait);
        }
    }

  nxsem_post(&priv->lock);

  return OK;
}

/****************************************************************************
 * Name: bat_gauge_open
 *
 * Description:
 *   This function is called whenever the battery device is opened.
 *
 ****************************************************************************/

static int bat_gauge_open(FAR struct file *filep)
{
  FAR struct battery_gauge_priv_s *priv;
  FAR struct battery_gauge_dev_s *dev = filep->f_inode->i_private;
  int ret;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  ret = nxsem_wait(&dev->batsem);
  if (ret < 0)
    {
      kmm_free(priv);
      return ret;
    }

  nxsem_init(&priv->lock, 0, 1);
  nxsem_init(&priv->wait, 0, 0);
  nxsem_set_protocol(&priv->wait, SEM_PRIO_NONE);
  list_add_tail(&dev->flist, &priv->node);
  nxsem_post(&dev->batsem);
  filep->f_priv = priv;

  return ret;
}

/****************************************************************************
 * Name: bat_gauge_close
 *
 * Description:
 *   This routine is called when the battery device is closed.
 *
 ****************************************************************************/

static int bat_gauge_close(FAR struct file *filep)
{
  FAR struct battery_gauge_priv_s *priv = filep->f_priv;
  FAR struct battery_gauge_dev_s *dev = filep->f_inode->i_private;
  int ret;

  ret = nxsem_wait(&dev->batsem);
  if (ret < 0)
    {
      return ret;
    }

  list_delete(&priv->node);
  nxsem_post(&dev->batsem);
  nxsem_destroy(&priv->lock);
  nxsem_destroy(&priv->wait);
  kmm_free(priv);

  return ret;
}

/****************************************************************************
 * Name: bat_gauge_read
 ****************************************************************************/

static ssize_t bat_gauge_read(FAR struct file *filep, FAR char *buffer,
                              size_t buflen)
{
  FAR struct battery_gauge_priv_s *priv = filep->f_priv;
  int ret;

  if (buflen < sizeof(priv->mask))
    {
      return -EINVAL;
    }

  ret = nxsem_wait(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  while (priv->mask == 0)
    {
      nxsem_post(&priv->lock);
      if (filep->f_oflags & O_NONBLOCK)
        {
          return -EAGAIN;
        }

      ret = nxsem_wait(&priv->wait);
      if (ret < 0)
        {
          return ret;
        }

      ret = nxsem_wait(&priv->lock);
      if (ret < 0)
        {
          return ret;
        }
    }

  memcpy(buffer, &priv->mask, sizeof(priv->mask));
  priv->mask = 0;

  nxsem_post(&priv->lock);
  return sizeof(priv->mask);
}

/****************************************************************************
 * Name: bat_gauge_write
 ****************************************************************************/

static ssize_t bat_gauge_write(FAR struct file *filep,
                               FAR const char *buffer,
                               size_t buflen)
{
  /* Return nothing written */

  return 0;
}

/****************************************************************************
 * Name: bat_gauge_ioctl
 ****************************************************************************/

static int bat_gauge_ioctl(FAR struct file *filep,
                           int cmd,
                           unsigned long arg)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct battery_gauge_dev_s *dev  = inode->i_private;
  int ret;

  /* Enforce mutually exclusive access to the battery driver */

  ret = nxsem_wait(&dev->batsem);
  if (ret < 0)
    {
      return ret; /* Probably -EINTR */
    }

  /* Procss the IOCTL command */

  ret = -EINVAL;  /* Assume a bad argument */
  switch (cmd)
    {
      case BATIOC_STATE:
        {
          FAR int *ptr = (FAR int *)((uintptr_t)arg);
          if (ptr)
            {
              ret = dev->ops->state(dev, ptr);
            }
        }
        break;

      case BATIOC_ONLINE:
        {
          FAR bool *ptr = (FAR bool *)((uintptr_t)arg);
          if (ptr)
            {
              ret = dev->ops->online(dev, ptr);
            }
        }
        break;

      case BATIOC_VOLTAGE:
        {
          FAR b16_t *ptr = (FAR b16_t *)((uintptr_t)arg);
          if (ptr)
            {
              ret = dev->ops->voltage(dev, ptr);
            }
        }
        break;

      case BATIOC_CAPACITY:
        {
          FAR b16_t *ptr = (FAR b16_t *)((uintptr_t)arg);
          if (ptr)
            {
              ret = dev->ops->capacity(dev, ptr);
            }
        }
        break;

        case BATIOC_CURRENT:
        {
          FAR b16_t *ptr = (FAR b16_t *)((uintptr_t)arg);
          if (ptr)
            {
              ret = dev->ops->current(dev, ptr);
            }
        }
        break;

        case BATIOC_TEMPERATURE:
        {
          FAR b8_t *ptr = (FAR b8_t *)((uintptr_t)arg);
          if (ptr)
            {
              ret = dev->ops->temp(dev, ptr);
            }
        }
        break;

      case BATIOC_CHIPID:
        {
          FAR unsigned int *ptr = (FAR unsigned int *)((uintptr_t)arg);
          if (ptr)
            {
              ret = dev->ops->chipid(dev, ptr);
            }
        }
        break;

      default:
        _err("ERROR: Unrecognized cmd: %d\n", cmd);
        ret = -ENOTTY;
        break;
    }

  nxsem_post(&dev->batsem);
  return ret;
}

/****************************************************************************
 * Name: bat_gauge_poll
 ****************************************************************************/

static ssize_t bat_gauge_poll(FAR struct file *filep,
                                struct pollfd *fds, bool setup)
{
  FAR struct battery_gauge_priv_s *priv = filep->f_priv;
  int ret;

  ret = nxsem_wait(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (setup)
    {
      if (priv->fds == NULL)
        {
          priv->fds = fds;
          fds->priv = &priv->fds;
        }
      else
        {
          ret = -EBUSY;
        }
    }
  else if (fds->priv != NULL)
    {
      priv->fds = NULL;
      fds->priv = NULL;
    }

  nxsem_post(&priv->lock);

  if (setup)
    {
      battery_gauge_notify(priv, 0);
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: battery_gauge_changed
 ****************************************************************************/

int battery_gauge_changed(FAR struct battery_gauge_dev_s *dev,
                             uint32_t mask)
{
  FAR struct battery_gauge_priv_s *priv;
  int ret;

  ret = nxsem_wait_uninterruptible(&dev->batsem);
  if (ret < 0)
    {
      return ret;
    }

  list_for_every_entry(&dev->flist, priv,
                       struct battery_gauge_priv_s, node)
    {
      battery_gauge_notify(priv, mask);
    }

  nxsem_post(&dev->batsem);
  return OK;
}

/****************************************************************************
 * Name: battery_gauge_register
 *
 * Description:
 *   Register a lower half battery driver with the common, upper-half
 *   battery driver.
 *
 * Input Parameters:
 *   devpath - The location in the pseudo-filesystem to create the driver.
 *     Recommended standard is "/dev/bat0", "/dev/bat1", etc.
 *   dev - An instance of the battery state structure .
 *
 * Returned Value:
 *    Zero on success or a negated errno value on failure.
 *
 ****************************************************************************/

int battery_gauge_register(FAR const char *devpath,
                           FAR struct battery_gauge_dev_s *dev)
{
  int ret;

  /* Initialize the semaphore and list */

  nxsem_init(&dev->batsem, 0, 1);
  list_initialize(&dev->flist);

  /* Register the character driver */

  ret = register_driver(devpath, &g_batteryops, 0555, dev);
  if (ret < 0)
    {
      _err("ERROR: Failed to register driver: %d\n", ret);
    }

  return ret;
}
#endif /* CONFIG_BATTERY_GAUGE */

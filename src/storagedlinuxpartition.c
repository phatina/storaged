/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <glib-unix.h>

#include <glib/gstdio.h>

#include "storagedlogging.h"
#include "storagedlinuxpartition.h"
#include "storagedlinuxblockobject.h"
#include "storageddaemon.h"
#include "storageddaemonutil.h"
#include "storagedlinuxdevice.h"
#include "storagedlinuxblock.h"

/**
 * SECTION:storagedlinuxpartition
 * @title: StoragedLinuxPartition
 * @short_description: Linux implementation of #StoragedPartition
 *
 * This type provides an implementation of the #StoragedPartition
 * interface on Linux.
 */

typedef struct _StoragedLinuxPartitionClass   StoragedLinuxPartitionClass;

/**
 * StoragedLinuxPartition:
 *
 * The #StoragedLinuxPartition structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxPartition
{
  StoragedPartitionSkeleton parent_instance;
};

struct _StoragedLinuxPartitionClass
{
  StoragedPartitionSkeletonClass parent_class;
};

static void partition_iface_init (StoragedPartitionIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxPartition, storaged_linux_partition, STORAGED_TYPE_PARTITION_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_PARTITION, partition_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_partition_init (StoragedLinuxPartition *partition)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (partition),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_partition_class_init (StoragedLinuxPartitionClass *klass)
{
}

/**
 * storaged_linux_partition_new:
 *
 * Creates a new #StoragedLinuxPartition instance.
 *
 * Returns: A new #StoragedLinuxPartition. Free with g_object_unref().
 */
StoragedPartition *
storaged_linux_partition_new (void)
{
  return STORAGED_PARTITION (g_object_new (STORAGED_TYPE_LINUX_PARTITION,
                                           NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_linux_partition_update:
 * @partition: A #StoragedLinuxPartition.
 * @object: The enclosing #StoragedLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_partition_update (StoragedLinuxPartition  *partition,
                                 StoragedLinuxBlockObject     *object)
{
  StoragedObject *disk_block_object = NULL;
  StoragedLinuxDevice *device = NULL;
  guint number = 0;
  const gchar *type = NULL;
  gchar type_buf[16];
  guint64 offset = 0;
  guint64 size = 0;
  gchar *name = NULL;
  const gchar *uuid = NULL;
  guint64 flags = 0;
  const gchar *table_object_path = "/";
  gboolean is_container = FALSE;
  gboolean is_contained = FALSE;

  device = storaged_linux_block_object_get_device (object);
  if (g_udev_device_has_property (device->udev_device, "ID_PART_ENTRY_TYPE"))
    {
      const gchar *disk_string;
      number = g_udev_device_get_property_as_int (device->udev_device, "ID_PART_ENTRY_NUMBER");
      type = g_udev_device_get_property (device->udev_device, "ID_PART_ENTRY_TYPE");
      offset = g_udev_device_get_property_as_uint64 (device->udev_device, "ID_PART_ENTRY_OFFSET") * G_GUINT64_CONSTANT (512);
      size = g_udev_device_get_property_as_uint64 (device->udev_device, "ID_PART_ENTRY_SIZE") * G_GUINT64_CONSTANT (512);
      name = storaged_decode_udev_string (g_udev_device_get_property (device->udev_device, "ID_PART_ENTRY_NAME"));
      uuid = g_udev_device_get_property (device->udev_device, "ID_PART_ENTRY_UUID");
      flags = g_udev_device_get_property_as_uint64 (device->udev_device, "ID_PART_ENTRY_FLAGS");

      disk_string = g_udev_device_get_property (device->udev_device, "ID_PART_ENTRY_DISK");
      if (disk_string != NULL)
        {
          gint disk_major, disk_minor;
          if (sscanf (disk_string, "%d:%d", &disk_major, &disk_minor) == 2)
            {
              disk_block_object = storaged_daemon_find_block (storaged_linux_block_object_get_daemon (object),
                                                            makedev (disk_major, disk_minor));
            }
        }

      if (g_strcmp0 (g_udev_device_get_property (device->udev_device, "ID_PART_ENTRY_SCHEME"), "dos") == 0)
        {
          char *endp;
          guint type_as_int = strtoul (type, &endp, 0);
          if (type[0] != '\0' && *endp == '\0')
            {
              /* ensure 'dos' partition types are always of the form 0x0c (e.g. with two digits) */
              snprintf (type_buf, sizeof type_buf, "0x%02x", type_as_int);
              type = type_buf;
              if (number <= 4)
                {
                  if (type_as_int == 0x05 || type_as_int == 0x0f || type_as_int == 0x85)
                    {
                      is_container = TRUE;
                    }
                }
              else if (number >= 5)
                {
                  is_contained = TRUE;
                }
            }
        }
    }
  else
    {
      GUdevDevice *parent_device;
      number = g_udev_device_get_sysfs_attr_as_int (device->udev_device, "partition");
      offset = g_udev_device_get_sysfs_attr_as_uint64 (device->udev_device, "start") * G_GUINT64_CONSTANT (512);
      size = g_udev_device_get_sysfs_attr_as_uint64 (device->udev_device, "size") * G_GUINT64_CONSTANT (512);
      parent_device = g_udev_device_get_parent_with_subsystem (device->udev_device, "block", "disk");
      if (parent_device != NULL)
        {
          disk_block_object = storaged_daemon_find_block (storaged_linux_block_object_get_daemon (object),
                                                          g_udev_device_get_device_number (parent_device));
          g_object_unref (parent_device);
        }
    }

  if (disk_block_object != NULL)
    table_object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (disk_block_object));

  storaged_partition_set_number (STORAGED_PARTITION (partition), number);
  storaged_partition_set_type_ (STORAGED_PARTITION (partition), type);
  storaged_partition_set_flags (STORAGED_PARTITION (partition), flags);
  storaged_partition_set_offset (STORAGED_PARTITION (partition), offset);
  storaged_partition_set_size (STORAGED_PARTITION (partition), size);
  storaged_partition_set_name (STORAGED_PARTITION (partition), name);
  storaged_partition_set_uuid (STORAGED_PARTITION (partition), uuid);
  storaged_partition_set_table (STORAGED_PARTITION (partition), table_object_path);
  storaged_partition_set_is_container (STORAGED_PARTITION (partition), is_container);
  storaged_partition_set_is_contained (STORAGED_PARTITION (partition), is_contained);

  g_free (name);
  g_clear_object (&device);
  g_clear_object (&disk_block_object);
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_set_flags (StoragedPartition        *partition,
                  GDBusMethodInvocation    *invocation,
                  guint64                   flags,
                  GVariant                 *options)
{
  const gchar *action_id = NULL;
  const gchar *message = NULL;
  StoragedBlock *block = NULL;
  StoragedObject *object = NULL;
  StoragedDaemon *daemon = NULL;
  gchar *error_message = NULL;
  gchar *escaped_device = NULL;
  StoragedObject *partition_table_object = NULL;
  StoragedPartitionTable *partition_table = NULL;
  StoragedBlock *partition_table_block = NULL;
  gchar *command_line = NULL;
  gint fd = -1;
  uid_t caller_uid;
  gid_t caller_gid;
  GError *error;

  error = NULL;
  object = storaged_daemon_util_dup_object (partition, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  block = storaged_object_get_block (object);

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 &caller_gid,
                                                 NULL,
                                                 &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  partition_table_object = storaged_daemon_find_object (daemon, storaged_partition_get_table (partition));
  partition_table = storaged_object_get_partition_table (partition_table_object);
  partition_table_block = storaged_object_get_block (partition_table_object);

  action_id = "org.storaged.Storaged.modify-device";
  /* Translators: Shown in authentication dialog when the user
   * requests modifying a partition (changing type, flags, name etc.).
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to modify the partition on device $(drive)");
  if (!storaged_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (storaged_block_get_hint_system (block))
        {
          action_id = "org.storaged.Storaged.modify-device-system";
        }
      else if (!storaged_daemon_util_on_user_seat (daemon, object, caller_uid))
        {
          action_id = "org.storaged.Storaged.modify-device-other-seat";
        }
    }

  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      object,
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (partition_table_block));
  if (g_strcmp0 (storaged_partition_table_get_type_ (partition_table), "gpt") == 0)
    {
      command_line = g_strdup_printf ("sgdisk --attributes %u:=:0x%08x%08x %s",
                                      storaged_partition_get_number (partition),
                                      (guint32) (flags >> 32),
                                      (guint32) (flags & 0xffffffff),
                                      escaped_device);
    }
  else if (g_strcmp0 (storaged_partition_table_get_type_ (partition_table), "dos") == 0)
    {
      command_line = g_strdup_printf ("parted --script %s \"set %u boot %s\"",
                                      escaped_device,
                                      storaged_partition_get_number (partition),
                                      flags & 0x80 ? "on" : "off");
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_NOT_SUPPORTED,
                                             "No support for modifying a partition a table of type `%s'",
                                             storaged_partition_table_get_type_ (partition_table));
      goto out;
    }

  /* hold a file descriptor open to suppress BLKRRPART generated by the tools */
  fd = open (storaged_block_get_device (block), O_RDONLY);

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                object,
                                                "partition-modify", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "%s",
                                                command_line))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error setting partition flags on %s: %s",
                                             storaged_block_get_device (block),
                                             error_message);
      goto out;
    }
  storaged_linux_block_object_trigger_uevent (STORAGED_LINUX_BLOCK_OBJECT (object));

  storaged_partition_complete_set_flags (partition, invocation);

 out:
  if (fd != -1)
    close (fd);
  g_free (command_line);
  g_free (escaped_device);
  g_free (error_message);
  g_clear_object (&object);
  g_clear_object (&block);
  g_clear_object (&partition_table_object);
  g_clear_object (&partition_table);
  g_clear_object (&partition_table_block);
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_set_name (StoragedPartition        *partition,
                 GDBusMethodInvocation    *invocation,
                 const gchar              *name,
                 GVariant                 *options)
{
  const gchar *action_id = NULL;
  const gchar *message = NULL;
  StoragedBlock *block = NULL;
  StoragedObject *object = NULL;
  StoragedDaemon *daemon = NULL;
  gchar *error_message = NULL;
  gchar *escaped_device = NULL;
  gchar *escaped_name = NULL;
  StoragedObject *partition_table_object = NULL;
  StoragedPartitionTable *partition_table = NULL;
  StoragedBlock *partition_table_block = NULL;
  gchar *command_line = NULL;
  gint fd = -1;
  uid_t caller_uid;
  gid_t caller_gid;
  GError *error;

  error = NULL;
  object = storaged_daemon_util_dup_object (partition, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  block = storaged_object_get_block (object);

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &caller_gid,
                                               NULL,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  partition_table_object = storaged_daemon_find_object (daemon, storaged_partition_get_table (partition));
  partition_table = storaged_object_get_partition_table (partition_table_object);
  partition_table_block = storaged_object_get_block (partition_table_object);

  action_id = "org.storaged.Storaged.modify-device";
  /* Translators: Shown in authentication dialog when the user
   * requests modifying a partition (changing type, flags, name etc.).
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to modify the partition on device $(drive)");
  if (!storaged_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (storaged_block_get_hint_system (block))
        {
          action_id = "org.storaged.Storaged.modify-device-system";
        }
      else if (!storaged_daemon_util_on_user_seat (daemon, object, caller_uid))
        {
          action_id = "org.storaged.Storaged.modify-device-other-seat";
        }
    }
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      object,
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (partition_table_block));
  escaped_name = storaged_daemon_util_escape_and_quote (name);
  if (g_strcmp0 (storaged_partition_table_get_type_ (partition_table), "gpt") == 0)
    {
      if (strlen (name) > 36)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 STORAGED_ERROR_FAILED,
                                                 "Max partition name length is 36 characters");
          goto out;
        }
      /* We are assuming that the sgdisk(8) command accepts UTF-8
       *
       * TODO is this assumption true or do we need to pass UTF-16? How is that going to work?
       */
      command_line = g_strdup_printf ("sgdisk --change-name %u:%s %s",
                                      storaged_partition_get_number (partition),
                                      escaped_name,
                                      escaped_device);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_NOT_SUPPORTED,
                                             "No support for modifying a partition a table of type `%s'",
                                             storaged_partition_table_get_type_ (partition_table));
      goto out;
    }

  /* hold a file descriptor open to suppress BLKRRPART generated by the tools */
  fd = open (storaged_block_get_device (block), O_RDONLY);

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                              object,
                                              "partition-modify", caller_uid,
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "%s",
                                              command_line))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error setting partition flags on %s: %s",
                                             storaged_block_get_device (block),
                                             error_message);
      goto out;
    }
  storaged_linux_block_object_trigger_uevent (STORAGED_LINUX_BLOCK_OBJECT (object));

  storaged_partition_complete_set_name (partition, invocation);

 out:
  if (fd != -1)
    close (fd);
  g_free (command_line);
  g_free (escaped_name);
  g_free (escaped_device);
  g_free (error_message);
  g_clear_object (&object);
  g_clear_object (&block);
  g_clear_object (&partition_table_object);
  g_clear_object (&partition_table);
  g_clear_object (&partition_table_block);
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_valid_uuid (const gchar *str)
{
  gboolean ret = FALSE;
  guint groups[] = {8, 4, 4, 4, 12, 0};
  guint pos, n, m;

  if (strlen (str) != 36)
    goto out;

  pos = 0;
  for (n = 0; groups[n] != 0; n++)
    {
      if (pos > 0)
        {
          if (str[pos++] != '-')
            goto out;
        }
      for (m = 0; m < groups[n]; m++)
        {
          if (!g_ascii_isxdigit (str[pos++]))
            goto out;
        }
    }

  ret = TRUE;

 out:
  return ret;
}

/**
 * storaged_linux_partition_set_type_sync:
 * @partition: A #StoragedLinuxPartition.
 * @type: The partition type to set.
 * @caller_uid: The uid of the process requesting this change or 0.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Sets the partition type. The calling thread is blocked while the
 * operation is pending.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if error is set.
 */
gboolean
storaged_linux_partition_set_type_sync (StoragedLinuxPartition  *partition,
                                        const gchar             *type,
                                        uid_t                    caller_uid,
                                        GCancellable            *cancellable,
                                        GError                 **error)
{
  gboolean ret = FALSE;
  StoragedBlock *block = NULL;
  StoragedObject *object = NULL;
  StoragedDaemon *daemon = NULL;
  gchar *error_message = NULL;
  gchar *escaped_device = NULL;
  gchar *escaped_type = NULL;
  StoragedObject *partition_table_object = NULL;
  StoragedPartitionTable *partition_table = NULL;
  StoragedBlock *partition_table_block = NULL;
  gchar *command_line = NULL;
  gint fd = -1;

  object = storaged_daemon_util_dup_object (partition, error);
  if (object == NULL)
    goto out;

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  block = storaged_object_get_block (object);

  partition_table_object = storaged_daemon_find_object (daemon, storaged_partition_get_table (STORAGED_PARTITION (partition)));
  partition_table = storaged_object_get_partition_table (partition_table_object);
  partition_table_block = storaged_object_get_block (partition_table_object);

  escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (partition_table_block));
  escaped_type = storaged_daemon_util_escape_and_quote (type);
  if (g_strcmp0 (storaged_partition_table_get_type_ (partition_table), "gpt") == 0)
    {
      /* check that it's a valid GUID */
      if (!is_valid_uuid (type))
        {
          g_set_error (error,
                       STORAGED_ERROR,
                       STORAGED_ERROR_FAILED,
                       "Given type `%s' is not a valid UUID",
                       type);
          goto out;
        }
      command_line = g_strdup_printf ("sgdisk --typecode %u:%s %s",
                                      storaged_partition_get_number (STORAGED_PARTITION (partition)),
                                      escaped_type,
                                      escaped_device);
    }
  else if (g_strcmp0 (storaged_partition_table_get_type_ (partition_table), "dos") == 0)
    {
      gchar *endp;
      guint type_as_int = strtoul (type, &endp, 0);
      if (strlen (type) == 0 || *endp != '\0')
        {
          g_set_error (error,
                       STORAGED_ERROR,
                       STORAGED_ERROR_FAILED,
                       "Given type `%s' is not a valid",
                       type);
          goto out;
        }
      if (type_as_int == 0x05 || type_as_int == 0x0f || type_as_int == 0x85)
        {
          g_set_error (error,
                       STORAGED_ERROR,
                       STORAGED_ERROR_FAILED,
                       "Refusing to change partition type to that of an extended partition. "
                       "Delete the partition and create a new extended partition instead.");
          goto out;
        }
      command_line = g_strdup_printf ("sfdisk --change-id %s %u 0x%02x",
                                      escaped_device,
                                      storaged_partition_get_number (STORAGED_PARTITION (partition)),
                                      type_as_int);
    }
  else
    {
      g_set_error (error,
                   STORAGED_ERROR,
                   STORAGED_ERROR_NOT_SUPPORTED,
                   "No support for modifying a partition a table of type `%s'",
                   storaged_partition_table_get_type_ (partition_table));
      goto out;
    }

  /* hold a file descriptor open to suppress BLKRRPART generated by the tools */
  fd = open (storaged_block_get_device (block), O_RDONLY);

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                object,
                                                "partition-modify", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "%s",
                                                command_line))
    {
      g_set_error (error,
                   STORAGED_ERROR,
                   STORAGED_ERROR_FAILED,
                   "Error setting partition flags on %s: %s",
                   storaged_block_get_device (block),
                   error_message);
      goto out;
    }
  storaged_linux_block_object_trigger_uevent (STORAGED_LINUX_BLOCK_OBJECT (object));

  ret = TRUE;

 out:
  if (fd != -1)
    close (fd);
  g_free (command_line);
  g_free (escaped_type);
  g_free (escaped_device);
  g_free (error_message);
  g_clear_object (&object);
  g_clear_object (&block);
  g_clear_object (&partition_table_object);
  g_clear_object (&partition_table);
  g_clear_object (&partition_table_block);
  g_clear_object (&object);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_set_type (StoragedPartition        *partition,
                 GDBusMethodInvocation    *invocation,
                 const gchar              *type,
                 GVariant                 *options)
{
  const gchar *action_id = NULL;
  const gchar *message = NULL;
  StoragedBlock *block = NULL;
  StoragedObject *object = NULL;
  StoragedDaemon *daemon = NULL;
  StoragedObject *partition_table_object = NULL;
  StoragedPartitionTable *partition_table = NULL;
  StoragedBlock *partition_table_block = NULL;
  uid_t caller_uid;
  gid_t caller_gid;
  GError *error;

  error = NULL;
  object = storaged_daemon_util_dup_object (partition, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  block = storaged_object_get_block (object);

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 &caller_gid,
                                                 NULL,
                                                 &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  partition_table_object = storaged_daemon_find_object (daemon, storaged_partition_get_table (partition));
  partition_table = storaged_object_get_partition_table (partition_table_object);
  partition_table_block = storaged_object_get_block (partition_table_object);

  action_id = "org.storaged.Storaged.modify-device";
  /* Translators: Shown in authentication dialog when the user
   * requests modifying a partition (changing type, flags, name etc.).
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to modify the partition on device $(drive)");
  if (!storaged_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (storaged_block_get_hint_system (block))
        {
          action_id = "org.storaged.Storaged.modify-device-system";
        }
      else if (!storaged_daemon_util_on_user_seat (daemon, object, caller_uid))
        {
          action_id = "org.storaged.Storaged.modify-device-other-seat";
        }
    }
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      object,
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  if (!storaged_linux_partition_set_type_sync (STORAGED_LINUX_PARTITION (partition), type, caller_uid, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_partition_complete_set_type (partition, invocation);

 out:
  g_clear_object (&object);
  g_clear_object (&block);
  g_clear_object (&partition_table_object);
  g_clear_object (&partition_table);
  g_clear_object (&partition_table_block);
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_delete (StoragedPartition        *partition,
               GDBusMethodInvocation    *invocation,
               GVariant                 *options)
{
  const gchar *action_id = NULL;
  const gchar *message = NULL;
  StoragedBlock *block = NULL;
  StoragedObject *object = NULL;
  StoragedDaemon *daemon = NULL;
  gchar *error_message = NULL;
  gchar *escaped_device = NULL;
  StoragedObject *partition_table_object = NULL;
  StoragedPartitionTable *partition_table = NULL;
  StoragedBlock *partition_table_block = NULL;
  gchar *command_line = NULL;
  uid_t caller_uid;
  gid_t caller_gid;
  gboolean teardown_flag = FALSE;
  GError *error;

  g_variant_lookup (options, "tear-down", "b", &teardown_flag);

  error = NULL;
  object = storaged_daemon_util_dup_object (partition, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  block = storaged_object_get_block (object);

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 &caller_gid,
                                                 NULL,
                                                 &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  partition_table_object = storaged_daemon_find_object (daemon, storaged_partition_get_table (partition));
  partition_table = storaged_object_get_partition_table (partition_table_object);
  partition_table_block = storaged_object_get_block (partition_table_object);

  action_id = "org.storaged.Storaged.modify-device";
  /* Translators: Shown in authentication dialog when the user
   * requests deleting a partition.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to delete the partition $(drive)");
  if (!storaged_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (storaged_block_get_hint_system (block))
        {
          action_id = "org.storaged.Storaged.modify-device-system";
        }
      else if (!storaged_daemon_util_on_user_seat (daemon, object, caller_uid))
        {
          action_id = "org.storaged.Storaged.modify-device-other-seat";
        }
    }
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      object,
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  if (teardown_flag)
    {
      if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                          NULL,
                                                          "org.storaged.Storaged.modify-system-configuration",
                                                          options,
                                                          N_("Authentication is required to modify the system configuration"),
                                                          invocation))
        goto out;

      if (!storaged_linux_block_teardown (block, invocation, options, &error))
        {
          if (invocation != NULL)
            g_dbus_method_invocation_take_error (invocation, error);
          else
            g_clear_error (&error);
          goto out;
        }
    }

  escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (partition_table_block));

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                partition_table_object,
                                                "partition-delete", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "parted --script %s \"rm %u\"",
                                                escaped_device,
                                                storaged_partition_get_number (partition)))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error deleting partition %s: %s",
                                             storaged_block_get_device (block),
                                             error_message);
      goto out;
    }
  /* this is sometimes needed because parted(8) does not generate the uevent itself */
  storaged_linux_block_object_trigger_uevent (STORAGED_LINUX_BLOCK_OBJECT (partition_table_object));

  storaged_partition_complete_delete (partition, invocation);

 out:
  g_free (command_line);
  g_free (escaped_device);
  g_free (error_message);
  g_clear_object (&object);
  g_clear_object (&block);
  g_clear_object (&partition_table_object);
  g_clear_object (&partition_table);
  g_clear_object (&partition_table_block);
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
partition_iface_init (StoragedPartitionIface *iface)
{
  iface->handle_set_flags = handle_set_flags;
  iface->handle_set_name  = handle_set_name;
  iface->handle_set_type  = handle_set_type;
  iface->handle_delete    = handle_delete;
}

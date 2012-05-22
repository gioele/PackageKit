/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU General Public License Version 2
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <config.h>
#include <gio/gio.h>
#include <pk-plugin.h>
#include <sqlite3.h>
#include <packagekit-glib2/pk-package.h>

struct PkPluginPrivate {
	PkPackageSack		*pkg_sack;
	GMainLoop		*loop;
};

/**
 * pk_plugin_get_description:
 */
const gchar *
pk_plugin_get_description (void)
{
	return "Maintains a database of all packages for fast read-only access to package information";
}

/**
 * pk_plugin_initialize:
 */
void
pk_plugin_initialize (PkPlugin *plugin)
{
	/* create private area */
	plugin->priv = PK_TRANSACTION_PLUGIN_GET_PRIVATE (PkPluginPrivate);
	plugin->priv->loop = g_main_loop_new (NULL, FALSE);
	plugin->priv->pkg_sack = pk_package_sack_new ();
}

/**
 * pk_plugin_destroy:
 */
void
pk_plugin_destroy (PkPlugin *plugin)
{
	g_main_loop_unref (plugin->priv->loop);
	g_object_unref (plugin->priv->pkg_sack);
}

/**
 * pk_plugin_package_cb:
 **/
static void
pk_plugin_package_cb (PkBackend *backend,
		      PkPackage *package,
		      PkPlugin *plugin)
{
	pk_package_sack_add_package (plugin->priv->pkg_sack, package);
}

/**
 * pk_plugin_finished_cb:
 **/
static void
pk_plugin_finished_cb (PkBackend *backend,
		       PkExitEnum exit_enum,
		       PkPlugin *plugin)
{
	if (!g_main_loop_is_running (plugin->priv->loop))
		return;
	g_main_loop_quit (plugin->priv->loop);
}

/**
 * pk_plugin_transaction_finished_end:
 */
void
pk_plugin_transaction_finished_end (PkPlugin *plugin,
				    PkTransaction *transaction)
{
	gboolean ret;
	guint finished_id = 0;
	guint package_id = 0;
	PkConf *conf;
	PkRoleEnum role;
	PkPluginPrivate *priv = plugin->priv;

	/* check the config file */
	conf = pk_transaction_get_conf (transaction);
	ret = pk_conf_get_bool (conf, "UpdatePackageList");
	if (!ret)
		goto out;

	/* check the role */
	role = pk_transaction_get_role (transaction);
	if (role != PK_ROLE_ENUM_REFRESH_CACHE)
		goto out;

	/* check we can do the action */
	if (!pk_backend_is_implemented (plugin->backend,
	    PK_ROLE_ENUM_GET_PACKAGES)) {
		g_debug ("cannot get packages");
		goto out;
	}

	/* connect to backend */
	finished_id = g_signal_connect (plugin->backend, "finished",
					G_CALLBACK (pk_plugin_finished_cb), plugin);
	package_id = g_signal_connect (plugin->backend, "package",
				       G_CALLBACK (pk_plugin_package_cb), plugin);

	g_debug ("plugin: recreating package database");

	/* clear old package-sack */
	pk_package_sack_clear (priv->pkg_sack);

	/* update UI */
	pk_backend_set_status (plugin->backend,
			       PK_STATUS_ENUM_GENERATE_PACKAGE_LIST);
	pk_backend_set_percentage (plugin->backend, 101);

	/* get the new package list */
	pk_backend_reset (plugin->backend);
	pk_backend_get_packages (plugin->backend, PK_FILTER_ENUM_NONE);

	/* wait for finished */
	g_main_loop_run (plugin->priv->loop);

	/* update UI */
	pk_backend_set_percentage (plugin->backend, 90);

	//gboolean	 pk_package_sack_get_details		(PkPackageSack		*package_sack,
	//						 GCancellable		*cancellable,
	//						 GError			**error);

	// TODO: Update DB here!

	/* update UI */
	pk_backend_set_percentage (plugin->backend, 100);
	pk_backend_set_status (plugin->backend, PK_STATUS_ENUM_FINISHED);

out:
	if (finished_id != 0) {
		g_signal_handler_disconnect (plugin->backend, finished_id);
		g_signal_handler_disconnect (plugin->backend, package_id);
	}
}

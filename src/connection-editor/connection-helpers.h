// SPDX-License-Identifier: GPL-2.0+
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Copyright 2012 - 2014 Red Hat, Inc.
 */

#ifndef __CONNECTION_HELPERS_H__
#define __CONNECTION_HELPERS_H__

#include <NetworkManager.h>

#include "ce-page.h"

typedef struct {
	const char *name;
	GType setting_types[4];
	PageNewConnectionFunc new_connection_func;
	gboolean virtual;
} ConnectionTypeData;

ConnectionTypeData *get_connection_type_list (void);

struct _func_tag_new_connection_type_filter;
#define FUNC_TAG_NEW_CONNECTION_TYPE_FILTER_IMPL struct _func_tag_new_connection_type_filter *_dummy
#define FUNC_TAG_NEW_CONNECTION_TYPE_FILTER_CALL ((struct _func_tag_new_connection_type_filter *) NULL)
typedef gboolean (*NewConnectionTypeFilterFunc) (FUNC_TAG_NEW_CONNECTION_TYPE_FILTER_IMPL,
                                                 GType type,
                                                 gpointer user_data);

struct _func_tag_new_connection_result;
#define FUNC_TAG_NEW_CONNECTION_RESULT_IMPL struct _func_tag_new_connection_result *_dummy
#define FUNC_TAG_NEW_CONNECTION_RESULT_CALL ((struct _func_tag_new_connection_result *) NULL)
typedef void (*NewConnectionResultFunc) (FUNC_TAG_NEW_CONNECTION_RESULT_IMPL,
                                         NMConnection *connection, /* allow-none, don't transfer reference, allow-keep */
                                         gpointer user_data);

void new_connection_dialog      (GtkWindow *parent_window,
                                 NMClient *client,
                                 NewConnectionTypeFilterFunc type_filter_func,
                                 NewConnectionResultFunc result_func,
                                 gpointer user_data);
void new_connection_dialog_full (GtkWindow *parent_window,
                                 NMClient *client,
                                 const char *primary_label,
                                 const char *secondary_label,
                                 NewConnectionTypeFilterFunc type_filter_func,
                                 NewConnectionResultFunc result_func,
                                 gpointer user_data);

void new_connection_of_type (GtkWindow *parent_window,
                             const char *detail,
                             gpointer detail_data,
                             NMConnection *connection,
                             NMClient *client,
                             PageNewConnectionFunc new_func,
                             NewConnectionResultFunc result_func,
                             gpointer user_data);

struct _func_tag_delete_connection_result;
#define FUNC_TAG_DELETE_CONNECTION_RESULT_IMPL struct _func_tag_delete_connection_result *_dummy
#define FUNC_TAG_DELETE_CONNECTION_RESULT_CALL ((struct _func_tag_delete_connection_result *) NULL)
typedef void (*DeleteConnectionResultFunc) (FUNC_TAG_DELETE_CONNECTION_RESULT_IMPL,
                                            NMRemoteConnection *connection,
                                            gboolean deleted,
                                            gpointer user_data);

void delete_connection (GtkWindow *parent_window,
                        NMRemoteConnection *connection,
                        DeleteConnectionResultFunc result_func,
                        gpointer user_data);

gboolean connection_supports_proxy (NMConnection *connection);
gboolean connection_supports_ip4 (NMConnection *connection);
gboolean connection_supports_ip6 (NMConnection *connection);

NMConnection *vpn_connection_from_file (const char *filename, GError **error);

#endif  /* __CONNECTION_HELPERS_H__ */


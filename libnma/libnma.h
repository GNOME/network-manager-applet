/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#ifndef NMA_LIB_H
#define NMA_LIB_H

#include <gtk/gtk.h>

#define WPA_KEY_TYPE_NAME_COL		0
#define WPA_KEY_TYPE_CIPHER_COL	1

#define WPA_EAP_NAME_COL		0
#define WPA_EAP_VALUE_COL	1

GtkTreeModel *wso_wpa_create_key_type_model    (int capabilities,
									   gboolean wpa_eap,
									   int *num_added);
gboolean      wso_wpa_key_type_get_iter        (GtkTreeModel *model,
									   uint key_type,
									   GtkTreeIter *iter);

GtkTreeModel *wso_wpa_create_phase2_type_model (int capabilities,
									   int *num_added);
gboolean      wso_wpa_phase2_type_get_iter     (GtkTreeModel *model,
									   uint phase2_type,
									   GtkTreeIter *iter);

GtkTreeModel *wso_wpa_create_eap_method_model  (void);
gboolean      wso_wpa_eap_method_get_iter      (GtkTreeModel *model,
									   uint eap_method,
									   GtkTreeIter *iter);

#define LEAP_KEY_MGMT_NAME_COL  0
#define LEAP_KEY_MGMT_VALUE_COL 1

GtkTreeModel *wso_leap_create_key_mgmt_model   (void);
gboolean      wso_leap_key_mgmt_get_iter       (GtkTreeModel *model,
									   const char *key_mgmt,
									   GtkTreeIter *iter);

#endif /* NMA_LIB_H */

/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Applet -- allow user control over networking
 *
 * Dan Williams <dcbw@redhat.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2009 Red Hat, Inc.
 */

#include <glib.h>
#include <string.h>

#include "utils.h"

typedef struct {
	char *foobar_infra_open;
	char *foobar_infra_wep;
	char *foobar_infra_wpa;
	char *foobar_infra_rsn;
	char *foobar_infra_wpa_rsn;
	char *foobar_adhoc_open;
	char *foobar_adhoc_wep;
	char *foobar_adhoc_wpa;
	char *foobar_adhoc_rsn;
	char *foobar_adhoc_wpa_rsn;

	char *asdf11_infra_open;
	char *asdf11_infra_wep;
	char *asdf11_infra_wpa;
	char *asdf11_infra_rsn;
	char *asdf11_infra_wpa_rsn;
	char *asdf11_adhoc_open;
	char *asdf11_adhoc_wep;
	char *asdf11_adhoc_wpa;
	char *asdf11_adhoc_rsn;
	char *asdf11_adhoc_wpa_rsn;
} TestData;

static GByteArray *
string_to_ssid (const char *str)
{
	GByteArray *ssid;

	g_assert (str != NULL);

	ssid = g_byte_array_sized_new (strlen (str));
	g_assert (ssid != NULL);
	g_byte_array_append (ssid, (const guint8 *) str, strlen (str));
	return ssid;
}

static char *
make_hash (const char *str,
           NM80211Mode mode,
           guint32 flags,
           guint32 wpa_flags,
           guint32 rsn_flags)
{
	GByteArray *ssid;
	char *hash, *hash2;

	ssid = string_to_ssid (str);

	hash = utils_hash_ap (ssid, mode, flags, wpa_flags, rsn_flags);
	g_assert (hash != NULL);

	hash2 = utils_hash_ap (ssid, mode, flags, wpa_flags, rsn_flags);
	g_assert (hash2 != NULL);

	/* Make sure they are the same each time */
	g_assert (!strcmp (hash, hash2));

	g_byte_array_free (ssid, TRUE);
	return hash;
}

static void
make_ssid_hashes (const char *ssid,
                  NM80211Mode mode,
                  char **open,
                  char **wep,
                  char **wpa,
                  char **rsn,
                  char **wpa_rsn)
{
	*open = make_hash (ssid, mode,
	                   NM_802_11_AP_FLAGS_NONE,
	                   NM_802_11_AP_SEC_NONE,
	                   NM_802_11_AP_SEC_NONE);

	*wep = make_hash (ssid, mode,
	                  NM_802_11_AP_FLAGS_PRIVACY,
	                  NM_802_11_AP_SEC_NONE,
	                  NM_802_11_AP_SEC_NONE);

	*wpa = make_hash (ssid, mode,
	                  NM_802_11_AP_FLAGS_PRIVACY,
	                  NM_802_11_AP_SEC_PAIR_TKIP |
	                      NM_802_11_AP_SEC_GROUP_TKIP |
	                      NM_802_11_AP_SEC_KEY_MGMT_PSK,
	                  NM_802_11_AP_SEC_NONE);

	*rsn = make_hash (ssid, mode,
	                  NM_802_11_AP_FLAGS_PRIVACY,
	                  NM_802_11_AP_SEC_NONE,
	                  NM_802_11_AP_SEC_PAIR_CCMP |
	                      NM_802_11_AP_SEC_GROUP_CCMP |
	                      NM_802_11_AP_SEC_KEY_MGMT_PSK);

	*wpa_rsn = make_hash (ssid, mode,
	                      NM_802_11_AP_FLAGS_PRIVACY,
	                      NM_802_11_AP_SEC_PAIR_TKIP |
	                          NM_802_11_AP_SEC_GROUP_TKIP |
	                          NM_802_11_AP_SEC_KEY_MGMT_PSK,
	                      NM_802_11_AP_SEC_PAIR_CCMP |
	                          NM_802_11_AP_SEC_GROUP_CCMP |
	                          NM_802_11_AP_SEC_KEY_MGMT_PSK);
}

static TestData *
test_data_new (void)
{
	TestData *d;

	d = g_malloc0 (sizeof (TestData));
	g_assert (d);

	make_ssid_hashes ("foobar", NM_802_11_MODE_INFRA,
	                  &d->foobar_infra_open,
	                  &d->foobar_infra_wep,
	                  &d->foobar_infra_wpa,
	                  &d->foobar_infra_rsn,
	                  &d->foobar_infra_wpa_rsn);

	make_ssid_hashes ("foobar", NM_802_11_MODE_ADHOC,
	                  &d->foobar_adhoc_open,
	                  &d->foobar_adhoc_wep,
	                  &d->foobar_adhoc_wpa,
	                  &d->foobar_adhoc_rsn,
	                  &d->foobar_adhoc_wpa_rsn);

	make_ssid_hashes ("asdf11", NM_802_11_MODE_INFRA,
	                  &d->asdf11_infra_open,
	                  &d->asdf11_infra_wep,
	                  &d->asdf11_infra_wpa,
	                  &d->asdf11_infra_rsn,
	                  &d->asdf11_infra_wpa_rsn);

	make_ssid_hashes ("asdf11", NM_802_11_MODE_ADHOC,
	                  &d->asdf11_adhoc_open,
	                  &d->asdf11_adhoc_wep,
	                  &d->asdf11_adhoc_wpa,
	                  &d->asdf11_adhoc_rsn,
	                  &d->asdf11_adhoc_wpa_rsn);

	return d;
}

static void
test_data_free (TestData *d)
{
	g_free (d->foobar_infra_open);
	g_free (d->foobar_infra_wep);
	g_free (d->foobar_infra_wpa);
	g_free (d->foobar_infra_rsn);
	g_free (d->foobar_infra_wpa_rsn);
	g_free (d->foobar_adhoc_open);
	g_free (d->foobar_adhoc_wep);
	g_free (d->foobar_adhoc_wpa);
	g_free (d->foobar_adhoc_rsn);
	g_free (d->foobar_adhoc_wpa_rsn);

	g_free (d->asdf11_infra_open);
	g_free (d->asdf11_infra_wep);
	g_free (d->asdf11_infra_wpa);
	g_free (d->asdf11_infra_rsn);
	g_free (d->asdf11_infra_wpa_rsn);
	g_free (d->asdf11_adhoc_open);
	g_free (d->asdf11_adhoc_wep);
	g_free (d->asdf11_adhoc_wpa);
	g_free (d->asdf11_adhoc_rsn);
	g_free (d->asdf11_adhoc_wpa_rsn);

	g_free (d);
}

static void
test_ap_hash_infra_adhoc_open (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_open, d->foobar_adhoc_open));
}

static void
test_ap_hash_infra_adhoc_wep (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_wep, d->foobar_adhoc_wep));
}

static void
test_ap_hash_infra_adhoc_wpa (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_wpa, d->foobar_adhoc_wpa));
}

static void
test_ap_hash_infra_adhoc_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_rsn, d->foobar_adhoc_rsn));
}

static void
test_ap_hash_infra_adhoc_wpa_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_wpa_rsn, d->foobar_adhoc_wpa_rsn));
}

static void
test_ap_hash_infra_open_wep (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_open, d->foobar_infra_wep));
}

static void
test_ap_hash_infra_open_wpa (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_open, d->foobar_infra_wpa));
}

static void
test_ap_hash_infra_open_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_open, d->foobar_infra_rsn));
}

static void
test_ap_hash_infra_open_wpa_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_open, d->foobar_infra_wpa_rsn));
}

static void
test_ap_hash_infra_wep_wpa (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_wep, d->foobar_infra_wpa));
}

static void
test_ap_hash_infra_wep_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_wep, d->foobar_infra_rsn));
}

static void
test_ap_hash_infra_wep_wpa_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_wep, d->foobar_infra_wpa_rsn));
}

static void
test_ap_hash_infra_wpa_rsn (void *f, TestData *d)
{
	/* these should be the same as we group all WPA/RSN APs together */
	g_assert (!strcmp (d->foobar_infra_wpa, d->foobar_infra_rsn));
}

static void
test_ap_hash_infra_wpa_wpa_rsn (void *f, TestData *d)
{
	/* these should be the same as we group all WPA/RSN APs together */
	g_assert (!strcmp (d->foobar_infra_wpa, d->foobar_infra_wpa_rsn));
}

static void
test_ap_hash_infra_rsn_wpa_rsn (void *f, TestData *d)
{
	/* these should be the same as we group all WPA/RSN APs together */
	g_assert (!strcmp (d->foobar_infra_rsn, d->foobar_infra_wpa_rsn));
}

static void
test_ap_hash_adhoc_open_wep (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_open, d->foobar_adhoc_wep));
}

static void
test_ap_hash_adhoc_open_wpa (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_open, d->foobar_adhoc_wpa));
}

static void
test_ap_hash_adhoc_open_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_open, d->foobar_adhoc_rsn));
}

static void
test_ap_hash_adhoc_open_wpa_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_open, d->foobar_adhoc_wpa_rsn));
}

static void
test_ap_hash_adhoc_wep_wpa (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_wep, d->foobar_adhoc_wpa));
}

static void
test_ap_hash_adhoc_wep_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_wep, d->foobar_adhoc_rsn));
}

static void
test_ap_hash_adhoc_wep_wpa_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_wep, d->foobar_adhoc_wpa_rsn));
}

static void
test_ap_hash_adhoc_wpa_rsn (void *f, TestData *d)
{
	/* these should be the same as we group all WPA/RSN APs together */
	g_assert (!strcmp (d->foobar_adhoc_wpa, d->foobar_adhoc_rsn));
}

static void
test_ap_hash_adhoc_wpa_wpa_rsn (void *f, TestData *d)
{
	/* these should be the same as we group all WPA/RSN APs together */
	g_assert (!strcmp (d->foobar_adhoc_wpa, d->foobar_adhoc_wpa_rsn));
}

static void
test_ap_hash_adhoc_rsn_wpa_rsn (void *f, TestData *d)
{
	/* these should be the same as we group all WPA/RSN APs together */
	g_assert (!strcmp (d->foobar_adhoc_rsn, d->foobar_adhoc_wpa_rsn));
}

static void
test_ap_hash_foobar_asdf11_infra_open (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_open, d->asdf11_infra_open));
}

static void
test_ap_hash_foobar_asdf11_infra_wep (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_wep, d->asdf11_infra_wep));
}

static void
test_ap_hash_foobar_asdf11_infra_wpa (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_wpa, d->asdf11_infra_wpa));
}

static void
test_ap_hash_foobar_asdf11_infra_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_rsn, d->asdf11_infra_rsn));
}

static void
test_ap_hash_foobar_asdf11_infra_wpa_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_infra_wpa_rsn, d->asdf11_infra_wpa_rsn));
}

static void
test_ap_hash_foobar_asdf11_adhoc_open (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_open, d->asdf11_adhoc_open));
}

static void
test_ap_hash_foobar_asdf11_adhoc_wep (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_wep, d->asdf11_adhoc_wep));
}

static void
test_ap_hash_foobar_asdf11_adhoc_wpa (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_wpa, d->asdf11_adhoc_wpa));
}

static void
test_ap_hash_foobar_asdf11_adhoc_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_rsn, d->asdf11_adhoc_rsn));
}

static void
test_ap_hash_foobar_asdf11_adhoc_wpa_rsn (void *f, TestData *d)
{
	g_assert (strcmp (d->foobar_adhoc_wpa_rsn, d->asdf11_adhoc_wpa_rsn));
}

#if GLIB_CHECK_VERSION(2,25,12)
typedef GTestFixtureFunc TCFunc;
#else
typedef void (*TCFunc)(void);
#endif

#define TESTCASE(t, d) g_test_create_case (#t, 0, d, NULL, (TCFunc) t, NULL)

int main (int argc, char **argv)
{
	GTestSuite *suite;
	gint result;
	TestData *data;

	g_test_init (&argc, &argv, NULL);

	suite = g_test_get_root ();
	data = test_data_new ();

	/* Test that hashes are different with the same SSID but different AP flags */
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_adhoc_open, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_adhoc_wep, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_adhoc_wpa, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_adhoc_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_adhoc_wpa_rsn, data));

	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_open_wep, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_open_wpa, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_open_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_open_wpa_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_wep_wpa, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_wep_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_wep_wpa_rsn, data));

	g_test_suite_add (suite, TESTCASE (test_ap_hash_adhoc_open_wep, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_adhoc_open_wpa, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_adhoc_open_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_adhoc_open_wpa_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_adhoc_wep_wpa, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_adhoc_wep_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_adhoc_wep_wpa_rsn, data));

	/* Test that wpa, rsn, and wpa_rsn all have the same hash */
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_wpa_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_wpa_wpa_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_infra_rsn_wpa_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_adhoc_wpa_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_adhoc_wpa_wpa_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_adhoc_rsn_wpa_rsn, data));

	/* Test that hashes are different with the same AP flags but different SSID */
	g_test_suite_add (suite, TESTCASE (test_ap_hash_foobar_asdf11_infra_open, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_foobar_asdf11_infra_wep, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_foobar_asdf11_infra_wpa, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_foobar_asdf11_infra_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_foobar_asdf11_infra_wpa_rsn, data));

	g_test_suite_add (suite, TESTCASE (test_ap_hash_foobar_asdf11_adhoc_open, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_foobar_asdf11_adhoc_wep, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_foobar_asdf11_adhoc_wpa, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_foobar_asdf11_adhoc_rsn, data));
	g_test_suite_add (suite, TESTCASE (test_ap_hash_foobar_asdf11_adhoc_wpa_rsn, data));

	result = g_test_run ();

	test_data_free (data);

	return result;
}


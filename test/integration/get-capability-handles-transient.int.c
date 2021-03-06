/*
 * Copyright (c) 2017, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <glib.h>
#include <inttypes.h>

#include "tcti-tabrmd.h"
#include "tpm2-struct-init.h"
#include "common.h"

#define NUM_KEYS 5
#define ENV_NUM_KEYS "TABRMD_TEST_NUM_KEYS"

/*
 * Creates a pile of keys. handles[0] will always be the primary. All others
 * are children of this primary.
 */
TSS2_RC
create_keys (TSS2_SYS_CONTEXT *sapi_context,
             TPM2_HANDLE       *handles[],
             size_t            count)
{
    TPM2B_PRIVATE      out_private = TPM2B_PRIVATE_STATIC_INIT;
    TPM2B_PUBLIC       out_public  = { 0 };
    TSS2_RC            rc          = TSS2_RC_SUCCESS;

    rc = create_primary (sapi_context, handles [0]);
    if (rc != TSS2_RC_SUCCESS)
        g_error ("Failed to create primary key: 0x%" PRIx32, rc);
    g_print ("primary handle: 0x%" PRIx32 "\n", *handles [0]);

    /*
     * Loop iteration starts @ 1 since we've already created the primary and
     * stored its handle in handles [0].
     */
    guint i;
    for (i = 1; i < count; ++i) {
        rc = create_key (sapi_context,
                         (*handles) [0],
                         &out_private,
                         &out_public);
        if (rc != TSS2_RC_SUCCESS)
            g_error ("Failed to create_key: 0x%" PRIx32, rc);
        rc = load_key (sapi_context,
                       (*handles) [0],
                       &(*handles) [i],
                       &out_private,
                       &out_public);
        if (rc != TSS2_RC_SUCCESS)
            g_error ("Failed to create_key: 0x%" PRIx32, rc);
        out_public.size = 0;
    }

    return rc;
}
/*
 * Query the TPM for transient object handles. These are returned to the
 * caller in the `handles` parameter array. The `handle_count` parameter
 * must hold the size of the handles array passed in. The function will
 * update this value to the number of handles returned.
 */
TSS2_RC
get_transient_handles (TSS2_SYS_CONTEXT *sapi_context,
                       TPM2_HANDLE        handles[],
                       size_t           *handle_count)
{
    TSS2_RC              rc          = TSS2_RC_SUCCESS;
    TPMI_YES_NO          more_data   = NO;
    TPMS_CAPABILITY_DATA cap_data    = { 0, };
    size_t               handles_left = *handle_count;
    size_t               handles_got = 0;

    handles [handles_got] = TPM2_TRANSIENT_FIRST;
    do {
        g_print ("requesting %zu handles from TPM\n", handles_left);
        rc = Tss2_Sys_GetCapability (sapi_context,
                                     NULL,
                                     TPM2_CAP_HANDLES,
                                     handles [handles_got],
                                     handles_left,
                                     &more_data,
                                     &cap_data,
                                     NULL);
        if (rc != TSS2_RC_SUCCESS) {
            return rc;
        }
        if (cap_data.capability != TPM2_CAP_HANDLES) {
            g_error ("got weird capability: 0x%" PRIx32,
                     cap_data.capability);
        }
        g_print ("got %" PRIu32 " handles from TPM\n",
                 cap_data.data.handles.count);
        if (*handle_count < handles_got + cap_data.data.handles.count) {
            g_warning ("too many handles for the array");
            return 1;
        }
        size_t i;
        for (i = 0; i < cap_data.data.handles.count; ++i) {
            g_print ("  0x%" PRIx32 "\n", cap_data.data.handles.handle [i]);
            handles [handles_got + i] = cap_data.data.handles.handle [i];
        }
        handles_got += cap_data.data.handles.count;
        handles_left -= handles_got;
        more_data == YES ? g_print ("more data\n") : g_print ("no more data\n");
    } while (more_data == YES);

    *handle_count = handles_got + 1;

    return TSS2_RC_SUCCESS;
}
/*
 * Query the TPM for transient object handles and dump them to stdout. 'count'
 * handles are retrieved for each call to GetCapability untill we've obtained
 * all handles.
 */
TSS2_RC
get_cap_trans_dump (TSS2_SYS_CONTEXT *sapi_context,
                    size_t count)
{
    TSS2_RC              rc          = TSS2_RC_SUCCESS;
    TPMI_YES_NO          more_data   = NO;
    TPMS_CAPABILITY_DATA cap_data    = { 0, };
    TPM2_HANDLE           last_handle = TPM2_TRANSIENT_FIRST;

    do {
        g_print ("requesting %zu handles from TPM\n", count);
        rc = Tss2_Sys_GetCapability (sapi_context,
                                     NULL,
                                     TPM2_CAP_HANDLES,
                                     last_handle,
                                     count,
                                     &more_data,
                                     &cap_data,
                                     NULL);
        if (rc != TSS2_RC_SUCCESS) {
            return rc;
        }
        if (cap_data.capability != TPM2_CAP_HANDLES) {
            g_error ("got weird capability: 0x%" PRIx32,
                     cap_data.capability);
        }
        g_print ("got %" PRIu32 " handles from TPM\n",
                 cap_data.data.handles.count);
        size_t i;
        for (i = 0; i < cap_data.data.handles.count; ++i) {
            g_print ("  0x%" PRIx32 "\n", cap_data.data.handles.handle [i]);
            /* add one to last handle to get the next handle */
            last_handle = cap_data.data.handles.handle [i] + 1;
        }
        more_data == YES ? g_print ("more data\n") : g_print ("no more data\n");
    } while (more_data == YES);

    return TSS2_RC_SUCCESS;
}

/*
 * This is a test program that creates and loads a configurable number of
 * transient objects in the NULL hierarchy. The number of keys created
 * undert the NULL primary key can be provided as a base 10 integer on
 * the command line. This is the only parameter the program takes.
 */
int
test_invoke (TSS2_SYS_CONTEXT *sapi_context)
{
    TPM2_HANDLE          *handles_load;
    TPM2_HANDLE          *handles_query;
    size_t               handles_count;
    char                *env_str         = NULL, *end_ptr = NULL;
    uint8_t              loops           = NUM_KEYS;
    TSS2_RC              rc              = TSS2_RC_SUCCESS;

    env_str = getenv (ENV_NUM_KEYS);
    if (env_str != NULL)
        loops = strtol (env_str, &end_ptr, 10);

    g_print ("%s: %d\n", ENV_NUM_KEYS, loops);

    handles_load = calloc (loops, sizeof (TPM2_HANDLE));
    handles_query = calloc (loops, sizeof (TPM2_HANDLE));
    handles_count = loops;

    rc = create_keys (sapi_context, &handles_load, handles_count);
    if (rc != TSS2_RC_SUCCESS) {
        return rc;
    }
    g_debug ("iterating over handles:");
    size_t i;
    for (i = 0; i < loops; ++i) {
        g_print ("handle [%zu]: 0x%" PRIx32 "\n", i, handles_load [i]);
    }
    g_print ("quering handles with GetCapability in increments of 2\n");

    rc = get_cap_trans_dump (sapi_context, 2);
    if (rc != TSS2_RC_SUCCESS) {
        g_warning ("get_cap_trans_dump returned 0x%" PRIx32, rc);
        return rc;
    }

    rc = get_transient_handles (sapi_context, handles_query, &handles_count);
    if (rc != TSS2_RC_SUCCESS) {
        g_warning ("get_transient_handles returned 0x%" PRIx32, rc);
        return rc;
    }

    g_debug ("loaded handle count: %zu", handles_count);
    for (i = 0; i < handles_count; ++i) {
        g_debug ("handles_load [%zu]: 0x%" PRIx32
                 " handles_query [%zu]: 0x%" PRIx32,
                 i, handles_load [i], i, handles_query [i]);
        if (handles_load [i] != handles_query [i]) {
            return -1;
        }
    }

    return 0;
}

/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr_strings.h"
#include "apr_portable.h"
#include "apr_user.h"
#include "apr_private.h"
#include "apr_arch_utf8.h"
#if APR_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

APR_DECLARE(apr_status_t) apr_gid_get(apr_gid_t *gid, 
                                      const char *groupname, apr_pool_t *p)
{
#ifdef _WIN32_WCE
    return APR_ENOTIMPL;
#elif ! UNICODE
#error Linden apr_gid_get() requires wchar_t support
#else
    SID_NAME_USE sidtype;
    wchar_t anydomain[256];
    wchar_t *domain;
    DWORD sidlen = 0;
    DWORD domlen = sizeof(anydomain)/sizeof(anydomain[0]);
    DWORD rv;
    wchar_t *pos;

    /* convert UTF8 groupname to wchar_t */
    wchar_t wgroupname[256], *pgroupname;
    apr_size_t groupname_len = strlen(groupname);
    apr_size_t wgroupname_size = sizeof(wgroupname)/sizeof(wgroupname[0]);
    apr_conv_utf8_to_ucs2(groupname, &groupname_len, wgroupname, &wgroupname_size);

    if ((pos = wcschr(wgroupname, L'/')) ||
        (pos = wcschr(wgroupname, L'\\'))) {
        *pos = L'\0';
        domain = wgroupname;
        pgroupname = pos + 1;
    }
    else {
        domain = NULL;
        pgroupname = wgroupname;
    }
    /* Get nothing on the first pass ... need to size the sid buffer 
     */
    rv = LookupAccountNameW(domain, pgroupname, domain, &sidlen, 
                            anydomain, &domlen, &sidtype);
    if (sidlen) {
        /* Give it back on the second pass
         */
        *gid = apr_palloc(p, sidlen);
        domlen = sizeof(anydomain)/sizeof(anydomain[0]);
        rv = LookupAccountNameW(domain, pgroupname, *gid, &sidlen, 
                                anydomain, &domlen, &sidtype);
    }
    if (!sidlen || !rv) {
        return apr_get_os_error();
    }
    return APR_SUCCESS;
#endif
}

APR_DECLARE(apr_status_t) apr_gid_name_get(char **groupname, apr_gid_t groupid, apr_pool_t *p)
{
#ifdef _WIN32_WCE
    *groupname = apr_pstrdup(p, "Administrators");
#elif ! UNICODE
#error Linden apr_gid_name_get() requires wchar_t support
#else
    SID_NAME_USE type;
    wchar_t wname[MAX_PATH], domain[MAX_PATH];
    DWORD cbname = sizeof(wname)/sizeof(wname[0]), cbdomain = sizeof(domain)/sizeof(domain[0]);
    char name[MAX_PATH];
    apr_size_t wname_len;
    apr_size_t name_size = sizeof(name);

    if (!groupid)
        return APR_EINVAL;
    if (!LookupAccountSidW(NULL, groupid, wname, &cbname, domain, &cbdomain, &type))
        return apr_get_os_error();
    if (type != SidTypeGroup && type != SidTypeWellKnownGroup 
                             && type != SidTypeAlias)
        return APR_EINVAL;
    /* convert returned wname from wchar_t to char */
    wname_len = wcslen(wname);
    apr_conv_ucs2_to_utf8(wname, &wname_len, name, &name_size);
    *groupname = apr_pstrdup(p, name);
#endif
    return APR_SUCCESS;
}
  
APR_DECLARE(apr_status_t) apr_gid_compare(apr_gid_t left, apr_gid_t right)
{
    if (!left || !right)
        return APR_EINVAL;
#ifndef _WIN32_WCE
    if (!IsValidSid(left) || !IsValidSid(right))
        return APR_EINVAL;
    if (!EqualSid(left, right))
        return APR_EMISMATCH;
#endif
    return APR_SUCCESS;
}

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

#include "apr_private.h"
#include "apr_strings.h"
#include "apr_portable.h"
#include "apr_user.h"
#include "apr_arch_file_io.h"
#include "apr_arch_utf8.h"
#if APR_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifndef _WIN32_WCE
/* Internal sid binary to string translation, see MSKB Q131320.
 * Several user related operations require our SID to access
 * the registry, but in a string format.  All error handling
 * depends on IsValidSid(), which internally we better test long
 * before we get here!
 */
static void get_sid_string(char *buf, apr_size_t blen, apr_uid_t id)
{
    PSID_IDENTIFIER_AUTHORITY psia;
    DWORD nsa;
    DWORD sa;
    int slen;

    /* Determine authority values (these is a big-endian value, 
     * and NT records the value as hex if the value is > 2^32.)
     */
    psia = GetSidIdentifierAuthority(id);
    nsa =  (DWORD)(psia->Value[5])        + ((DWORD)(psia->Value[4]) <<  8)
        + ((DWORD)(psia->Value[3]) << 16) + ((DWORD)(psia->Value[2]) << 24);
    sa  =  (DWORD)(psia->Value[1])        + ((DWORD)(psia->Value[0]) <<  8);
    if (sa) {
        slen = apr_snprintf(buf, blen, "S-%d-0x%04x%08x",
                            SID_REVISION, (unsigned int)sa, (unsigned int)nsa);
    } else {
        slen = apr_snprintf(buf, blen, "S-%d-%lu",
                            SID_REVISION, nsa);
    }

    /* Now append all the subauthority strings.
     */
    nsa = *GetSidSubAuthorityCount(id);
    for (sa = 0; sa < nsa; ++sa) {
        slen += apr_snprintf(buf + slen, blen - slen, "-%lu",
                             *GetSidSubAuthority(id, sa));
    }
} 
#endif
/* Query the ProfileImagePath from the version-specific branch, where the
 * regkey uses the user's name on 9x, and user's sid string on NT.
 */
APR_DECLARE(apr_status_t) apr_uid_homepath_get(char **dirname, 
                                               const char *username, 
                                               apr_pool_t *p)
{
#ifdef _WIN32_WCE
    *dirname = apr_pstrdup(p, "/My Documents");
    return APR_SUCCESS;
#else
    apr_status_t rv;
    char regkey[MAX_PATH * 2];
    char *fixch;
    DWORD keylen;
    DWORD type;
    HKEY key;

    if (apr_os_level >= APR_WIN_NT) {
        apr_uid_t uid;
        apr_gid_t gid;
    
        if ((rv = apr_uid_get(&uid, &gid, username, p)) != APR_SUCCESS)
            return rv;

        strcpy(regkey, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\"
                       "ProfileList\\");
        keylen = (DWORD)strlen(regkey);
        get_sid_string(regkey + keylen, sizeof(regkey) - keylen, uid);
    }
    else {
        strcpy(regkey, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\"
                       "ProfileList\\");
        keylen = (DWORD)strlen(regkey);
        /* Because this code path is only for Windows older than NT, we do not
           care about the possibility of a non-ASCII username here. */
        apr_cpystrn(regkey + keylen, username, sizeof(regkey) - keylen);
    }

    /* regkey is specifically char[], so explicitly call RegOpenKeyExA() */
    if ((rv = RegOpenKeyExA(HKEY_LOCAL_MACHINE, regkey, 0, 
                           KEY_QUERY_VALUE, &key)) != ERROR_SUCCESS)
        return APR_FROM_OS_ERROR(rv);

#if APR_HAS_UNICODE_FS
    IF_WIN_OS_IS_UNICODE
    {
        keylen = sizeof(regkey);
        rv = RegQueryValueExW(key, L"ProfileImagePath", NULL, &type,
                                   (void*)regkey, &keylen);
        RegCloseKey(key);
        if (rv != ERROR_SUCCESS)
            return APR_FROM_OS_ERROR(rv);
        if (type == REG_SZ) {
            char retdir[MAX_PATH];
            if ((rv = unicode_to_utf8_path(retdir, sizeof(retdir), 
                                           (apr_wchar_t*)regkey)) != APR_SUCCESS)
                return rv;
            *dirname = apr_pstrdup(p, retdir);
        }
        else if (type == REG_EXPAND_SZ) {
            apr_wchar_t path[MAX_PATH];
            char retdir[MAX_PATH];
            ExpandEnvironmentStringsW((apr_wchar_t*)regkey, path, 
                                      sizeof(path) / 2);
            if ((rv = unicode_to_utf8_path(retdir, sizeof(retdir), path))
                    != APR_SUCCESS)
                return rv;
            *dirname = apr_pstrdup(p, retdir);
        }
        else
            return APR_ENOENT;
    }
#endif
#if APR_HAS_ANSI_FS
    ELSE_WIN_OS_IS_ANSI
    {
        keylen = sizeof(regkey);
        rv = RegQueryValueExA(key, "ProfileImagePath", NULL, &type,
                                  (void*)regkey, &keylen);
        RegCloseKey(key);
        if (rv != ERROR_SUCCESS)
            return APR_FROM_OS_ERROR(rv);
        if (type == REG_SZ) {
            *dirname = apr_pstrdup(p, regkey);
        }
        else if (type == REG_EXPAND_SZ) {
            char path[MAX_PATH];
            ExpandEnvironmentStringsA(regkey, path, sizeof(path));
            *dirname = apr_pstrdup(p, path);
        }
        else
            return APR_ENOENT;
    }
#endif /* APR_HAS_ANSI_FS */
    for (fixch = *dirname; *fixch; ++fixch)
        if (*fixch == '\\')
            *fixch = '/';
    return APR_SUCCESS;
#endif /* _WIN32_WCE */
}

APR_DECLARE(apr_status_t) apr_uid_current(apr_uid_t *uid,
                                          apr_gid_t *gid,
                                          apr_pool_t *p)
{
#ifdef _WIN32_WCE
    return APR_ENOTIMPL;
#else
    HANDLE threadtok;
    DWORD needed;
    TOKEN_USER *usr;
    TOKEN_PRIMARY_GROUP *grp;
    apr_status_t rv;

    if(!OpenProcessToken(GetCurrentProcess(), STANDARD_RIGHTS_READ | READ_CONTROL | TOKEN_QUERY, &threadtok)) {
        return apr_get_os_error();
    }

    *uid = NULL;
    if (!GetTokenInformation(threadtok, TokenUser, NULL, 0, &needed)
        && (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        && (usr = apr_palloc(p, needed))
        && GetTokenInformation(threadtok, TokenUser, usr, needed, &needed)) {
        *uid = usr->User.Sid;
    }
    else {
        rv = apr_get_os_error();
        CloseHandle(threadtok);
        return rv;
    }

    if (!GetTokenInformation(threadtok, TokenPrimaryGroup, NULL, 0, &needed)
        && (GetLastError() == ERROR_INSUFFICIENT_BUFFER) 
        && (grp = apr_palloc(p, needed))
        && GetTokenInformation(threadtok, TokenPrimaryGroup, grp, needed, &needed)) {
        *gid = grp->PrimaryGroup;
    }
    else {
        rv = apr_get_os_error();
        CloseHandle(threadtok);
        return rv;
    }

    CloseHandle(threadtok);

    return APR_SUCCESS;
#endif 
}

APR_DECLARE(apr_status_t) apr_uid_get(apr_uid_t *uid, apr_gid_t *gid,
                                      const char *username, apr_pool_t *p)
{
#ifdef _WIN32_WCE
    return APR_ENOTIMPL;
#elif ! UNICODE
#error Linden apr_uid_get() requires wchar_t support
#else
    SID_NAME_USE sidtype;
    wchar_t anydomain[256];
    wchar_t *domain;
    DWORD sidlen = 0;
    DWORD domlen = sizeof(anydomain)/sizeof(anydomain[0]);
    DWORD rv;
    wchar_t *pos;

    /* convert UTF8 username to wchar_t */
    wchar_t wusername[256], *pusername;
    apr_size_t username_len = strlen(username);
    apr_size_t wusername_size = sizeof(wusername)/sizeof(wusername[0]);
    apr_conv_utf8_to_ucs2(username, &username_len, wusername, &wusername_size);

    if ((pos = wcschr(wusername, L'/')) ||
        (pos = wcschr(wusername, L'\\'))) {
        *pos = L'\0';
        domain = wusername;
        pusername = pos + 1;
    }
    else {
        domain = NULL;
        pusername = wusername;
    }
    /* Get nothing on the first pass ... need to size the sid buffer 
     */
    rv = LookupAccountNameW(domain, pusername, domain, &sidlen, 
                           anydomain, &domlen, &sidtype);
    if (sidlen) {
        /* Give it back on the second pass
         */
        *uid = apr_palloc(p, sidlen);
        domlen = sizeof(anydomain)/sizeof(anydomain[0]);
        rv = LookupAccountNameW(domain, pusername, *uid, &sidlen, 
                                anydomain, &domlen, &sidtype);
    }
    if (!sidlen || !rv) {
        return apr_get_os_error();
    }
    /* There doesn't seem to be a simple way to retrieve the primary group sid
     */
    *gid = NULL;
    return APR_SUCCESS;
#endif
}

APR_DECLARE(apr_status_t) apr_uid_name_get(char **username, apr_uid_t userid,
                                           apr_pool_t *p)
{
#ifdef _WIN32_WCE
    *username = apr_pstrdup(p, "Administrator");
    return APR_SUCCESS;
#elif ! UNICODE
#error Linden apr_uid_name_get() requires wchar_t support
#else
    SID_NAME_USE type;
    wchar_t wname[MAX_PATH], domain[MAX_PATH];
    DWORD cbname = sizeof(wname)/sizeof(wname[0]), cbdomain = sizeof(domain)/sizeof(domain[0]);
    char name[MAX_PATH];
    apr_size_t wname_len;
    apr_size_t name_size = sizeof(name);

    if (!userid)
        return APR_EINVAL;
    if (!LookupAccountSidW(NULL, userid, wname, &cbname, domain, &cbdomain, &type))
        return apr_get_os_error();
    if (type != SidTypeUser && type != SidTypeAlias && type != SidTypeWellKnownGroup)
        return APR_EINVAL;
    /* convert returned wname from wchar_t to char */
    wname_len = wcslen(wname);
    apr_conv_ucs2_to_utf8(wname, &wname_len, name, &name_size);
    *username = apr_pstrdup(p, name);
    return APR_SUCCESS;
#endif
}
  
APR_DECLARE(apr_status_t) apr_uid_compare(apr_uid_t left, apr_uid_t right)
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


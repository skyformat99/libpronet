/*
 * Copyright (C) 2018 Eric Tung <libpronet@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of LibProNet (http://www.libpro.org)
 */

#include "pro_a.h"
#include "pro_z.h"

#if defined(__cplusplus)
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////
////

#if !defined(WIN32) && !defined(_WIN32_WCE)

static
inline
char
PRO_CALLTYPE
tolower_i(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        c = c - 'A' + 'a';
    }

    return (c);
}

static
inline
char
PRO_CALLTYPE
toupper_i(char c)
{
    if (c >= 'a' && c <= 'z')
    {
        c = c - 'a' + 'A';
    }

    return (c);
}

#endif /* WIN32, _WIN32_WCE */

/////////////////////////////////////////////////////////////////////////////
////

#if !defined(WIN32) && !defined(_WIN32_WCE)

char*
strlwr(char* str)
{
    if (str != NULL)
    {
        for (int i = 0; str[i] != '\0'; ++i)
        {
            str[i] = tolower_i(str[i]);
        }
    }

    return (str);
}

char*
strupr(char* str)
{
    if (str != NULL)
    {
        for (int i = 0; str[i] != '\0'; ++i)
        {
            str[i] = toupper_i(str[i]);
        }
    }

    return (str);
}

#endif /* WIN32, _WIN32_WCE */

char*
strncpy_pro(char*       dest,
            size_t      destSize,
            const char* src)
{
    if (dest == NULL || destSize == 0)
    {
        return (NULL);
    }

    *dest = '\0';

    if (destSize == 1 || src == NULL)
    {
        return (dest);
    }

    return (strncat(dest, src, destSize - 1));
}

int
snprintf_pro(char*       dest,
             size_t      destSize,
             const char* format,
             ...)
{
    if (dest == NULL || destSize == 0)
    {
        return (0);
    }

    *dest = '\0';

    if (destSize == 1 || format == NULL)
    {
        return (0);
    }

    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(dest, destSize, format, ap);
    va_end(ap);

    dest[destSize - 1] = '\0';

    if (ret > (int)destSize - 1)
    {
        ret = (int)destSize - 1;
    }

    return (ret);
}

/////////////////////////////////////////////////////////////////////////////
////

#if defined(__cplusplus)
}
#endif

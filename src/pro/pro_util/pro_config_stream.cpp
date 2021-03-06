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
#include "pro_config_stream.h"
#include "pro_config_file.h"
#include "pro_memory_pool.h"
#include "pro_stl.h"
#include "pro_z.h"

/////////////////////////////////////////////////////////////////////////////
////

bool
CProConfigStream::BufToConfigs(const void*                     buf,
                               size_t                          size,
                               CProStlVector<PRO_CONFIG_ITEM>& configs)
{
    configs.clear();

    if (buf == NULL || size == 0)
    {
        return (true);
    }

    bool              ret = true;
    bool              bom = false;
    const char*       itr = (char*)buf;
    const char* const end = (char*)buf + size - 1;

    while (itr <= end)
    {
        const char* p = itr;
        const char* q = NULL;
        const char* r = NULL;

        for (r = p; r <= end; ++r)
        {
            if (*r == '\r' || *r == '\n')
            {
                if (r < end && *(r + 1) == '\n')
                {
                    ++r;
                }
                break;
            }
        }

        /*
         * found a line
         */
        if (r <= end)
        {
            q = r;
        }
        else
        {
            q = end;
        }
        itr = q + 1;

        /*
         * UTF-8 BOM
         */
        if (!bom)
        {
            bom = true;

            if (p + 2 <= q && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
            {
                p += 3;
            }
        }

        /*
         * trim
         */
        for (; p <= q; ++p)
        {
            if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            {
                break;
            }
        }

        /*
         * trim
         */
        for (; q >= p; --q)
        {
            if (*q != ' ' && *q != '\t' && *q != '\r' && *q != '\n')
            {
                break;
            }
        }

        /*
         * blanks
         */
        if (p > q)
        {
            continue;
        }

        /*
         * comments
         */
        if (p + 1 <= q && p[0] == '/' && p[1] == '/')
        {
            continue;
        }

        /*
         * comments
         */
        if (*p == '#' || *p == ';')
        {
            continue;
        }

        PRO_CONFIG_ITEM item;

        for (int i = 0; i < 2; ++i)
        {
            /*
             * open-quote
             */
            if (*p != '\"')
            {
                ret = false;
                break;
            }

            ++p;
            if (p > q)
            {
                ret = false;
                break;
            }

            /*
             * close-quote
             */
            for (r = p; r <= q; ++r)
            {
                if (*r == '\"')
                {
                    break;
                }
            }

            if (r > q)
            {
                ret = false;
                break;
            }

            if (i == 0)
            {
                item.configName.assign (p, (int)(r - p)); /* name */
            }
            else
            {
                item.configValue.assign(p, (int)(r - p)); /* value */
            }

            /*
             * trim
             */
            for (p = r + 1; p <= q; ++p)
            {
                if (*p != ' ' && *p != '\t')
                {
                    break;
                }
            }

            /*
             * blanks
             */
            if (p > q)
            {
                p = NULL;
                break;
            }

            /*
             * comments
             */
            if (p + 1 <= q && p[0] == '/' && p[1] == '/')
            {
                p = NULL;
                break;
            }

            /*
             * comments
             */
            if (*p == '#' || *p == ';')
            {
                p = NULL;
                break;
            }
        } /* end of for (...) */

        /*
         * a corrupt line
         */
        if (!ret || p != NULL)
        {
            ret = false;
            break;
        }

        configs.push_back(item);
    } /* end of while (...) */

    return (ret);
}

bool
CProConfigStream::StringToConfigs(const CProStlString&            str,
                                  CProStlVector<PRO_CONFIG_ITEM>& configs)
{
    configs.clear();

    if (str.empty())
    {
        return (true);
    }

    return (BufToConfigs(&str[0], str.length(), configs));
}

void
CProConfigStream::ConfigsToString(const CProStlVector<PRO_CONFIG_ITEM>& configs,
                                  CProStlString&                        str)
{
    str = "";

    int       i = 0;
    const int c = (int)configs.size();

    for (; i < c; ++i)
    {
        const PRO_CONFIG_ITEM& config = configs[i];

        str += '\"';
        str += config.configName;
        str += "\"\"";
        str += config.configValue;
        str += "\"\n";
    }
}

CProConfigStream::CProConfigStream()
{
    m_nextIndex = 0;
}

void
CProConfigStream::Add(const CProStlString& configName,
                      const CProStlString& configValue)
{
    PRO_CONFIG_ITEM config;
    config.configName  = configName;
    config.configValue = configValue;

    m_name2Configs[config.configName].push_back(config);

    if (m_name2Index.find(config.configName) == m_name2Index.end())
    {
        m_name2Index[config.configName] = m_nextIndex;
        ++m_nextIndex;
    }
}

void
CProConfigStream::Add(const CProStlString&                configName,
                      const CProStlVector<CProStlString>& configValues)
{
    int       i = 0;
    const int c = (int)configValues.size();

    for (; i < c; ++i)
    {
        Add(configName, configValues[i]);
    }
}

void
CProConfigStream::AddInt(const CProStlString& configName,
                         int                  configValue)
{
    char configValue2[64] = "";
    sprintf(configValue2, "%d", configValue);

    Add(configName, configValue2);
}

void
CProConfigStream::AddInt(const CProStlString&      configName,
                         const CProStlVector<int>& configValues)
{
    int       i = 0;
    const int c = (int)configValues.size();

    for (; i < c; ++i)
    {
        AddInt(configName, configValues[i]);
    }
}

void
CProConfigStream::AddUint(const CProStlString& configName,
                          unsigned int         configValue)
{
    char configValue2[64] = "";
    sprintf(configValue2, "%u", configValue);

    Add(configName, configValue2);
}

void
CProConfigStream::AddUint(const CProStlString&               configName,
                          const CProStlVector<unsigned int>& configValues)
{
    int       i = 0;
    const int c = (int)configValues.size();

    for (; i < c; ++i)
    {
        AddUint(configName, configValues[i]);
    }
}

void
CProConfigStream::AddInt64(const CProStlString& configName,
                           PRO_INT64            configValue)
{
    char configValue2[64] = "";
#if defined(WIN32) || defined(_WIN32_WCE)
    sprintf(configValue2, "%I64d", configValue);
#else
    sprintf(configValue2, "%lld" , configValue);
#endif

    Add(configName, configValue2);
}

void
CProConfigStream::AddInt64(const CProStlString&            configName,
                           const CProStlVector<PRO_INT64>& configValues)
{
    int       i = 0;
    const int c = (int)configValues.size();

    for (; i < c; ++i)
    {
        AddInt64(configName, configValues[i]);
    }
}

void
CProConfigStream::AddUint64(const CProStlString& configName,
                            PRO_UINT64           configValue)
{
    char configValue2[64] = "";
#if defined(WIN32) || defined(_WIN32_WCE)
    sprintf(configValue2, "%I64u", configValue);
#else
    sprintf(configValue2, "%llu" , configValue);
#endif

    Add(configName, configValue2);
}

void
CProConfigStream::AddUint64(const CProStlString&             configName,
                            const CProStlVector<PRO_UINT64>& configValues)
{
    int       i = 0;
    const int c = (int)configValues.size();

    for (; i < c; ++i)
    {
        AddUint64(configName, configValues[i]);
    }
}

void
CProConfigStream::Add(const PRO_CONFIG_ITEM& config)
{
    Add(config.configName, config.configValue);
}

void
CProConfigStream::Add(const CProStlVector<PRO_CONFIG_ITEM>& configs)
{
    int       i = 0;
    const int c = (int)configs.size();

    for (; i < c; ++i)
    {
        Add(configs[i]);
    }
}

void
CProConfigStream::Remove(const CProStlString& configName)
{
    m_name2Configs.erase(configName);
    m_name2Index.erase(configName);
}

void
CProConfigStream::Clear()
{
    m_name2Configs.clear();
    m_name2Index.clear();
}

void
CProConfigStream::Get_i(const CProStlString&            configName,
                        CProStlVector<PRO_CONFIG_ITEM>& configs) const
{
    configs.clear();

    CProStlMap<CProStlString, CProStlVector<PRO_CONFIG_ITEM> >::const_iterator const itr =
        m_name2Configs.find(configName);
    if (itr != m_name2Configs.end())
    {
        configs = itr->second;
    }
}

void
CProConfigStream::Get(const CProStlString& configName,
                      CProStlString&       configValue) const
{
    configValue = "";

    CProStlVector<PRO_CONFIG_ITEM> configs;
    Get_i(configName, configs);

    if (configs.size() > 0)
    {
        configValue = configs[0].configValue;
    }
}

void
CProConfigStream::Get(const CProStlString&          configName,
                      CProStlVector<CProStlString>& configValues) const
{
    configValues.clear();

    CProStlVector<PRO_CONFIG_ITEM> configs;
    Get_i(configName, configs);

    int       i = 0;
    const int c = (int)configs.size();

    for (; i < c; ++i)
    {
        configValues.push_back(configs[i].configValue);
    }
}

void
CProConfigStream::GetInt(const CProStlString& configName,
                         int&                 configValue) const
{
    configValue = 0;

    CProStlString configValue2 = "";
    Get(configName, configValue2);

    if (!configValue2.empty())
    {
        sscanf(configValue2.c_str(), "%d", &configValue);
    }
}

void
CProConfigStream::GetInt(const CProStlString& configName,
                         CProStlVector<int>&  configValues) const
{
    configValues.clear();

    CProStlVector<PRO_CONFIG_ITEM> configs;
    Get_i(configName, configs);

    int       i = 0;
    const int c = (int)configs.size();

    for (; i < c; ++i)
    {
        int configValue = 0;

        const PRO_CONFIG_ITEM& config = configs[i];
        if (!config.configValue.empty())
        {
            sscanf(config.configValue.c_str(), "%d", &configValue);
        }

        configValues.push_back(configValue);
    }
}

void
CProConfigStream::GetUint(const CProStlString& configName,
                          unsigned int&        configValue) const
{
    configValue = 0;

    CProStlString configValue2 = "";
    Get(configName, configValue2);

    if (!configValue2.empty())
    {
        sscanf(configValue2.c_str(), "%u", &configValue);
    }
}

void
CProConfigStream::GetUint(const CProStlString&         configName,
                          CProStlVector<unsigned int>& configValues) const
{
    configValues.clear();

    CProStlVector<PRO_CONFIG_ITEM> configs;
    Get_i(configName, configs);

    int       i = 0;
    const int c = (int)configs.size();

    for (; i < c; ++i)
    {
        unsigned int configValue = 0;

        const PRO_CONFIG_ITEM& config = configs[i];
        if (!config.configValue.empty())
        {
            sscanf(config.configValue.c_str(), "%u", &configValue);
        }

        configValues.push_back(configValue);
    }
}

void
CProConfigStream::GetInt64(const CProStlString& configName,
                           PRO_INT64&           configValue) const
{
    configValue = 0;

    CProStlString configValue2 = "";
    Get(configName, configValue2);

    if (!configValue2.empty())
    {
#if defined(WIN32) || defined(_WIN32_WCE)
        sscanf(configValue2.c_str(), "%I64d", &configValue);
#else
        sscanf(configValue2.c_str(), "%lld" , &configValue);
#endif
    }
}

void
CProConfigStream::GetInt64(const CProStlString&      configName,
                           CProStlVector<PRO_INT64>& configValues) const
{
    configValues.clear();

    CProStlVector<PRO_CONFIG_ITEM> configs;
    Get_i(configName, configs);

    int       i = 0;
    const int c = (int)configs.size();

    for (; i < c; ++i)
    {
        PRO_INT64 configValue = 0;

        const PRO_CONFIG_ITEM& config = configs[i];
        if (!config.configValue.empty())
        {
#if defined(WIN32) || defined(_WIN32_WCE)
            sscanf(config.configValue.c_str(), "%I64d", &configValue);
#else
            sscanf(config.configValue.c_str(), "%lld" , &configValue);
#endif
        }

        configValues.push_back(configValue);
    }
}

void
CProConfigStream::GetUint64(const CProStlString& configName,
                            PRO_UINT64&          configValue) const
{
    configValue = 0;

    CProStlString configValue2 = "";
    Get(configName, configValue2);

    if (!configValue2.empty())
    {
#if defined(WIN32) || defined(_WIN32_WCE)
        sscanf(configValue2.c_str(), "%I64u", &configValue);
#else
        sscanf(configValue2.c_str(), "%llu" , &configValue);
#endif
    }
}

void
CProConfigStream::GetUint64(const CProStlString&       configName,
                            CProStlVector<PRO_UINT64>& configValues) const
{
    configValues.clear();

    CProStlVector<PRO_CONFIG_ITEM> configs;
    Get_i(configName, configs);

    int       i = 0;
    const int c = (int)configs.size();

    for (; i < c; ++i)
    {
        PRO_UINT64 configValue = 0;

        const PRO_CONFIG_ITEM& config = configs[i];
        if (!config.configValue.empty())
        {
#if defined(WIN32) || defined(_WIN32_WCE)
            sscanf(config.configValue.c_str(), "%I64u", &configValue);
#else
            sscanf(config.configValue.c_str(), "%llu" , &configValue);
#endif
        }

        configValues.push_back(configValue);
    }
}

void
CProConfigStream::Get(CProStlVector<PRO_CONFIG_ITEM>& configs) const
{
    configs.clear();

    CProStlMap<unsigned long, CProStlString> index2Name;

    {
        CProStlMap<CProStlString, unsigned long>::const_iterator       itr = m_name2Index.begin();
        CProStlMap<CProStlString, unsigned long>::const_iterator const end = m_name2Index.end();

        for (; itr != end; ++itr)
        {
            index2Name[itr->second] = itr->first;
        }
    }

    {
        CProStlMap<unsigned long, CProStlString>::const_iterator       itr = index2Name.begin();
        CProStlMap<unsigned long, CProStlString>::const_iterator const end = index2Name.end();

        for (; itr != end; ++itr)
        {
            CProStlMap<CProStlString, CProStlVector<PRO_CONFIG_ITEM> >::const_iterator const itr2 =
                m_name2Configs.find(itr->second);
            if (itr2 != m_name2Configs.end())
            {
                const CProStlVector<PRO_CONFIG_ITEM>& configs2 = itr2->second;

                configs.insert(
                    configs.begin() + configs.size(),
                    configs2.begin(),
                    configs2.begin() + configs2.size()
                    );
            }
        }
    }
}

void
CProConfigStream::ToString(CProStlString& theString) const
{
    CProStlVector<PRO_CONFIG_ITEM> theConfigs;
    Get(theConfigs);

    ConfigsToString(theConfigs, theString);
}

/*
*
*   Copyright (C) 2017 Sergey Shramchenko
*   https://github.com/srg70/pvr.puzzle.tv
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with XBMC; see the file COPYING.  If not, write to
*  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*  http://www.gnu.org/copyleft/gpl.html
*
*/


#include "globals.hpp"
#include "p8-platform/util/util.h"
#include "p8-platform/util/StringUtils.h"
#include "httplib.h"

namespace Globals
{
    static IAddonDelegate* __pvr = nullptr;
//    static ADDON::CHelper_libXBMC_addon* __xbmc = nullptr;
//    static CHelper_libKODI_guilib* __gui = nullptr;
//
    IAddonDelegate* const& PVR(__pvr);
//    ADDON::CHelper_libXBMC_addon* const& XBMC(__xbmc);
//    CHelper_libKODI_guilib* const& GUI(__gui);

    static AddonLog  __debugLogLevel = ADDON_LOG_DEBUG;
   
    void Cleanup()
    {
        __pvr = nullptr;
    //        if(__pvr)
    //            SAFE_DELETE(__pvr);
    //        if(__xbmc)
    //            SAFE_DELETE(__xbmc);
    //        if(__gui)
    //            SAFE_DELETE(__gui);
    //
    }

    void CreateWithHandle(IAddonDelegate* pvr)
    {
        Cleanup();
//        __xbmc = new ADDON::CHelper_libXBMC_addon();
//        if (!__xbmc->RegisterMe(hdl))
//        {
//            SAFE_DELETE(__xbmc);
//            return false;
//        }
//        
        __pvr = pvr;
//        if (!__pvr->RegisterMe(hdl))
//        {
//            SAFE_DELETE(__pvr);
//            SAFE_DELETE(__xbmc);
//            return false;
//        }
//        
//        __gui = new CHelper_libKODI_guilib();
//        if (!__gui->RegisterMe(hdl))
//        {
//            SAFE_DELETE(__pvr);
//            SAFE_DELETE(__xbmc);
//            SAFE_DELETE(__gui);
//            return false;
//        }
//        
    }
//    
    
    kodi::vfs::CFile* XBMC_OpenFile(const std::string& path, unsigned int flags)
    {
        auto f = new kodi::vfs::CFile();
        if(f->OpenFile(httplib::detail::encode_get_url(path).c_str(), flags))
            return f;
        return nullptr;
    }

# define PrintToLog(loglevel) \
std::string strData; \
strData.reserve(16384); \
va_list va; \
va_start(va, format); \
strData = StringUtils::FormatV(format,va); \
va_end(va); \
kodi::Log(loglevel, strData.c_str()); \

    
    void LogFatal(const char *format, ... )
    {
        PrintToLog(ADDON_LOG_FATAL);
    }
    void LogError(const char *format, ... )
    {
        PrintToLog(ADDON_LOG_ERROR);
    }
    void LogInfo(const char *format, ... )
    {
        PrintToLog(ADDON_LOG_INFO);
    }
    void LogNotice(const char *format, ... )
    {
        PrintToLog(ADDON_LOG_WARNING);
    }
    void LogDebug(const char *format, ... )
    {
        PrintToLog(__debugLogLevel);
    }
    

}

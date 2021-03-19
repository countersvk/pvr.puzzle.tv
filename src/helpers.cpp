/*
 *
 *   Copyright (C) 2017 Sergey Shramchenko
 *   https://github.com/srg70/pvr.puzzle.tv
 *
 *  Copyright (C) 2013 Alex Deryskyba (alex@codesnake.com)
 *  https://bitbucket.org/codesnake/pvr.sovok.tv_xbmc_addon
 *
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

#include <stdio.h>
#include <stdlib.h>
#include "helpers.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/document.h"
#include "globals.hpp"

namespace Helpers{ 
    
    std::string n_to_string(int64_t n)
    {
        char buffer[15];
        snprintf(buffer, sizeof(buffer), "%lld", n);
        
        return buffer;
    }
    
    std::string n_to_string_hex(uint64_t n)
    {
        char buffer[9];
        snprintf(buffer, sizeof(buffer), "%llX", n);
        
        return buffer;
    }
    
    std:: string time_t_to_string(const time_t& time)
    {
        char mbstr[100];
        
        if (std::strftime(mbstr, sizeof(mbstr), "%d/%m/%y %H:%M", std::localtime(&time))) {
            return mbstr;
        }
        return std::string("Wrong time format");
    }
    
    template <class T>
    void dump_json(const T& jValue)
    {
        using namespace rapidjson;
        
        StringBuffer sb;
        PrettyWriter<StringBuffer> writer(sb);
        jValue.Accept(writer);
        auto str = sb.GetString();
        printf("%s\n", str);
    }
    
    template void dump_json<rapidjson::Document>(const rapidjson::Document& jValue);
    template void dump_json<rapidjson::Value>(const rapidjson::Value& jValue);
    //int strtoi(const std::string &str)
    //{
    //    return atoi(str.c_str());
    //}
} //Helpers

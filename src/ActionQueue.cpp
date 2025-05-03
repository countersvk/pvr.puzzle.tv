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

#include "ActionQueue.hpp"
#include <kodi/AddonBase.h> // Добавляем новый заголовок для Kodi 20+

using namespace kodi; // Используем пространство имен Kodi

namespace ActionQueue {
    
    static const int32_t INFINITE_QUEUE_TIMEOUT = 0x7FFFFFFF;
    
    void* CActionQueue::Process()
    {
        // Активируем отладочное имя потока через API Kodi
        if(!_name.empty()){
            SetThreadName(GetCurrentThread(), _name.c_str());
            _name.clear();
        }

        while (!IsStopped() || (_willStop && !_actions.IsEmpty()))
        {
            IActionQueueItem* action = nullptr;
            if( _actions.Pop(action, (_willStop) ? 0 : INFINITE_QUEUE_TIMEOUT))
            {
                if(!action) continue;
                
                try {
                    if(_willStop)
                        action->Cancel();
                    else
                        action->Perform();
                } 
                catch (const std::exception& e) {
                    Log(ADDON_LOG_ERROR, "Action failed: %s", e.what());
                }
                
                delete action;
            }
            
            // Обработка приоритетных задач с использованием std::mutex
            std::lock_guard<std::mutex> lock(_priorityActionMutex);
            if(_priorityAction) {
                try {
                    _priorityAction->Perform();
                } 
                catch (const std::exception& e) {
                    Log(ADDON_LOG_ERROR, "Priority action failed: %s", e.what());
                }
                delete _priorityAction;
                _priorityAction = nullptr;
            }
        }
        return nullptr;
    }
    
    void CActionQueue::TerminatePipeline()
    {
        std::lock_guard<std::mutex> lock(_stateMutex); // Добавляем синхронизацию
        _willStop = true;
        _actions.Interrupt(); // Новый метод для прерывания ожидания
    }
    
    bool CActionQueue::StopThread(int iWaitMs)
    {
        TerminatePipeline();
        return Join(std::chrono::milliseconds(iWaitMs)); // Используем стандартный метод
    }
    
    CActionQueue::~CActionQueue(void)
    {
        StopThread(5000);
        if (_priorityAction) {
            delete _priorityAction;
            _priorityAction = nullptr;
        }
    }
    
}

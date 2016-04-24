#include "task.h"
#include <iostream>
#include <string.h>
#include <string>
#include <algorithm>
#include "scheduler.h"

namespace co
{

uint64_t Task::s_id = 0;
std::atomic<uint64_t> Task::s_task_count{0};

LFLock Task::s_stat_lock;
std::set<Task*> Task::s_stat_set;

void Task::Task_CB()
{
    if (g_Scheduler.GetOptions().exception_handle == eCoExHandle::immedaitely_throw) {
        fn_();
    } else {
        try {
            fn_();
        } catch (std::exception& e) {
            switch (g_Scheduler.GetOptions().exception_handle) {
                case eCoExHandle::immedaitely_throw:
                    throw ;
                    break;

                case eCoExHandle::delay_rethrow:
                    eptr_ = std::current_exception();
                    break;

                default:
                case eCoExHandle::debugger_only:
                    DebugPrint(dbg_exception|dbg_task, "task(%s) has uncaught exception:%s",
                            DebugInfo(), e.what());
                    break;
            }
        } catch (...) {
            switch (g_Scheduler.GetOptions().exception_handle) {
                case eCoExHandle::immedaitely_throw:
                    throw ;
                    break;

                case eCoExHandle::delay_rethrow:
                    eptr_ = std::current_exception();
                    break;

                default:
                case eCoExHandle::debugger_only:
                    DebugPrint(dbg_exception|dbg_task, "task(%s) has uncaught exception.", DebugInfo());
                    break;
            }
        }
    }

    state_ = TaskState::done;
    Scheduler::getInstance().CoYield();
}

Task::Task(TaskF const& fn, std::size_t stack_size, const char* file, int lineno)
    : id_(++s_id), ctx_(stack_size, [this]{Task_CB();}), fn_(fn)
{
    ++s_task_count;
    InitLocation(file, lineno);
    DebugPrint(dbg_task, "task(%s) construct. this=%p", DebugInfo(), this);
}

Task::~Task()
{
    assert(!this->prev);
    assert(!this->next);
    assert(!this->check_);
    assert(s_task_count > 0);

    if (Scheduler::getInstance().GetOptions().enable_coro_stat) {
        std::unique_lock<LFLock> lock(s_stat_lock);
        s_stat_set.erase(this);
    }

    --s_task_count;

    DebugPrint(dbg_task, "task(%s) destruct. this=%p", DebugInfo(), this);
}

void Task::InitLocation(const char* file, int lineno)
{
    this->location_.Init(file, lineno);
    if (Scheduler::getInstance().GetOptions().enable_coro_stat) {
        std::unique_lock<LFLock> lock(s_stat_lock);
        s_stat_set.insert(this);
    }
}

void Task::AddIntoProcesser(Processer *proc)
{
    assert(!proc_);
    proc_ = proc;
    state_ = TaskState::runnable;
}

bool Task::SwapIn()
{
    return ctx_.SwapIn();
}
bool Task::SwapOut()
{
    return ctx_.SwapOut();
}

void Task::SetDebugInfo(std::string const& info)
{
    debug_info_ = info + "(" + std::to_string(id_) + ")";
}

const char* Task::DebugInfo()
{
    if (debug_info_.empty()) {
        debug_info_ = std::to_string(id_);
        if (location_.file_)
            debug_info_ += " :" + location_.to_string();
    }

    return debug_info_.c_str();
}

uint64_t Task::GetTaskCount()
{
    return s_task_count;
}

std::map<SourceLocation, uint32_t> Task::GetStatInfo()
{
    std::map<SourceLocation, uint32_t> result;
    if (!Scheduler::getInstance().GetOptions().enable_coro_stat)
        return result;

    std::unique_lock<LFLock> lock(s_stat_lock);
    for (auto tk : s_stat_set)
    {
        ++result[tk->location_];
    }
    return result;
}

} //namespace co

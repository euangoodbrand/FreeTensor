#ifndef FREE_TENSOR_MEMORIZED_SCHEDULES_H
#define FREE_TENSOR_MEMORIZED_SCHEDULES_H

#include <mutex>
#include <unordered_set>

#include <schedule/schedule_log.h>

namespace freetensor {

/**
 * Storage of all tried scheduling decisions for all `Schedule`s `fork`ed from a
 * common one
 *
 * When we are searching for best scheduling decisions, we often try similar
 * ones, for example `Original -> Schedule A -> Schedule B` and `Original ->
 * Schedule A -> Schedule C`. Looking up from this class saves time for applying
 * identical decisions
 *
 * This class is thread-safe
 *
 * This class is not named cache or storage, to avoid confusion with hardware
 * features
 */
class MemoizedSchedules {
    std::unordered_set<ScheduleLog> memoized_;
    std::mutex lock_;

  public:
    /**
     * Lookup for a particular schedule
     *
     * If there is a memoized result, return the memoized one to save memory
     * (so the shared linked lists form a tree). If not found, save and return
     * the new log
     */
    ScheduleLog lookupOrCreate(const ScheduleLog &log) {
        std::lock_guard<std::mutex> guard(lock_);
        if (auto it = memoized_.find(log); it != memoized_.end()) {
            return *it;
        } else {
            memoized_.insert(log);
            return log;
        }
    }
};

} // namespace freetensor

#endif // FREE_TENSOR_MEMORIZED_SCHEDULES_H

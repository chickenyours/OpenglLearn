#pragma once

#include "engine/ECS/JobSystem/job_system_schedule.h"
#include "engine/ECS/Context/context.h"

namespace ECS::Core{
    class ECSKernel{
        private:
            JobSystemSchedule jobSchedule_;
        public:
            ECSKernel(){}
            // workerCount == 0 uses the JobSystemSchedule default policy.
            void Init(std::size_t workerCount = 0){
                jobSchedule_.Start(workerCount);
                globalECSCoreContext.jobSystemSchedule = &jobSchedule_;
            }
    };
}
#pragma once

#include "engine/ECS/JobSystem/job_system_schedule.h"
#include "engine/ECS/Context/context.h"

namespace ECS::Core{
    class ECSKernel{
        private:
            JobSystemSchedule jobSchedule_;
        public:
            ECSKernel(){}
            void Init(){
                jobSchedule_.Start(8);
                globalECSCoreContext.jobSystemSchedule = &jobSchedule_;
            }
    };
}
#pragma once

#include "engine/ECS/JobSystem/job_system_schedule.h"

namespace ECS::Core{
    class ECSKernel{
        private:
            JobSystemSchedule jobSchedule_;
        public:
            ECSKernel(){}
            void Init(){
                
            }
    };
}
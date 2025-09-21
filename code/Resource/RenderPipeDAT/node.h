#include <string>
#include <vector>

#include "code/Resource/RenderPipeDAT/Context/node_context.h"

namespace DATNode{
    template <typename In,typename Out, typename Depend>
    class Node{
        public:     
            In input;
            Out output;
            Depend depend;
            std::vector<Node*> relies;
            int CallInit(){
                return Init();
            }
            int CallSet(){
                return Set();
            };
            int CallUpdate(){
                for(auto it : relies){
                    int state = it->CallUpdate();
                    if(state){
                        return state;
                    }
                }
                return CallUpdate();
            }   
        protected:
            virtual int Init(const NodeContext& cxt){
                return 0;
            }
            virtual int Set(const NodeContext& cxt){
                return 0;
            }
            virtual int Update(){
                return 0;
            }
    };
}



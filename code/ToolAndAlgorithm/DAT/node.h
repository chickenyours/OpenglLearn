#include <string>
#include <vector>

template <typename In,typename Out, typename Depend>
class Node{
    public:     
        In input_;
        Out output_;
        std::vector<Node*> relies;
        void CallInit(){
            Init();
        }
        void CallUpdate(){
            for(auto it : relies){
                it->CallInit();
            }
        }   
    protected:
        virtual void Init(){}
        virtual void Update(){}
};


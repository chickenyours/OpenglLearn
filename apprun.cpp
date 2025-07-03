#include "code/Application/application.h"

int main(){
    Application app;
    if(app.Init()){
        app.Run();
    }
    return 0;
}
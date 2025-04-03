#include "pass.h"

using namespace Render;

Pass::Pass(){}

void Pass::Release(){}

Pass::~Pass(){
    Release();
}
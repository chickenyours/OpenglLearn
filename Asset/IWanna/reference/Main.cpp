//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
#include "CommonAPI.h"
#include "LessonX.h"
#include "stdio.h"
#include "windows.h"
#include "mmsystem.h"
#pragma comment(lib,"Winmm.lib")
float		g_fScreenLeft		=	0.f;    // 屏幕左边界值
float		g_fScreenRight		=	0.f;    // 右
float		g_fScreenTop		=	0.f;    // 上
float		g_fScreenBottom	=	0.f;
float		g_fSpeedLeft	=	0.f;  	// 左方向速度
float		g_fSpeedRight	=	0.f;  	// 右
float		g_fSpeedTop		=	0.f;  	// 上
float		g_fSpeedBottom	=	0.f;  	// 下
//静止小人的坐标
float       g_fManX=0.f;
float       g_fManY=0.f;

float       g_fKeepX=-71.351;
float       g_fKeepY=23.764;
float       g_fTu1X=-23.967;
float       g_fTu1Y=-19.756;
float       g_fTu2X=-17.5;
float       g_fTu2Y=-19.755;
int key=0;
float       g_fApple1X=67.5;
float       g_fApple1Y=-7.5;
float       g_fApple2X=-48.565;
float       g_fApple2Y=22.5;
float       g_fApple3X=59.480;
float       g_fApple3Y=-7.5;
char  szName[128];
int i=0;
float       g_fDManX=0.f;
float       g_fDManY=0.f;
int key1=0;
int only=0;
int only1=0;
int only2=0;
const char *g_sRunSpriteName = "move";
const char* g_sManSpriteName = "man";
void running()
{
    if((g_fSpeedLeft + g_fSpeedRight) < 0)
    {
        dSetSpriteFlipX(g_sManSpriteName, true);
        dSetSpriteFlipX(g_sRunSpriteName, true);
    }
    else if((g_fSpeedLeft + g_fSpeedRight) > 0)
    {
        dSetSpriteFlipX(g_sManSpriteName, false);
        dSetSpriteFlipX(g_sRunSpriteName, false);
    }


}
///////////////////////////////////////////////////////////////////////////////////////////
//
// 主函数入口
//
//////////////////////////////////////////////////////////////////////////////////////////
int PASCAL WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPSTR     lpCmdLine,
                   int       nCmdShow)

// 初始化游戏引擎
{

    if( !dInitGameEngine( hInstance, lpCmdLine ) )
        return 0;

    // To do : 在此使用API更改窗口标题
    dSetWindowTitle("i wanna be the king");
    g_fScreenLeft	 	= 	dGetScreenLeft();
    g_fScreenRight  	= 	dGetScreenRight();
    g_fScreenTop 	 	= 	dGetScreenTop();
    g_fScreenBottom 	= 	dGetScreenBottom();
    dSetSpriteWorldLimit(g_sManSpriteName, WORLD_LIMIT_NULL, g_fScreenLeft-23, g_fScreenTop, g_fScreenRight+24, g_fScreenBottom);
    dSetSpriteVisible("end1",0);
    dSetSpriteVisible("end2",0);
    dSetSpriteVisible(g_sRunSpriteName,0);

    // 引擎主循环，处理屏幕图像刷新等工作
    while( dEngineMainLoop() )
    {
        // 获取两次调用之间的时间差，传递给游戏逻辑处理
        float	fTimeDelta	=	dGetTimeDelta();

        // 执行游戏主循环
        GameMainLoop( fTimeDelta );
    };

    // 关闭游戏引擎
    dShutdownGameEngine();
    return 0;
}

//==========================================================================
//
// 引擎捕捉鼠标移动消息后，将调用到本函数
// 参数 fMouseX, fMouseY：为鼠标当前坐标
//
void dOnMouseMove( const float fMouseX, const float fMouseY )
{
    // 可以在此添加游戏需要的响应函数
    OnMouseMove(fMouseX, fMouseY );
}
//==========================================================================
//
// 引擎捕捉鼠标点击消息后，将调用到本函数
// 参数 iMouseType：鼠标按键值，见 enum MouseTypes 定义
// 参数 fMouseX, fMouseY：为鼠标当前坐标
//
void dOnMouseClick( const int iMouseType, const float fMouseX, const float fMouseY )
{
    // 可以在此添加游戏需要的响应函数
    OnMouseClick(iMouseType, fMouseX, fMouseY);

}
//==========================================================================
//
// 引擎捕捉鼠标弹起消息后，将调用到本函数
// 参数 iMouseType：鼠标按键值，见 enum MouseTypes 定义
// 参数 fMouseX, fMouseY：为鼠标当前坐标
//
void dOnMouseUp( const int iMouseType, const float fMouseX, const float fMouseY )
{
    // 可以在此添加游戏需要的响应函数
    OnMouseUp(iMouseType, fMouseX, fMouseY);

}
//==========================================================================
//
// 引擎捕捉键盘按下消息后，将调用到本函数
// 参数 iKey：被按下的键，值见 enum KeyCodes 宏定义
// 参数 iAltPress, iShiftPress，iCtrlPress：键盘上的功能键Alt，Ctrl，Shift当前是否也处于按下状态(0未按下，1按下)
//
void dOnKeyDown( const int iKey, const int iAltPress, const int iShiftPress, const int iCtrlPress )
{
    // 可以在此添加游戏需要的响应函数
    switch(iKey)
    {


    case KEY_J:
        if(key==0)
        {
            PlaySound("game/data/audio/jump1.wav",0,1);
            g_fSpeedTop = -75.f;
            key++;
        }
        else if(key==1)
        {
            PlaySound("game/data/audio/jump2.wav",0,1);
            g_fSpeedTop = -75.f;
            key++;
        }
        else if(g_fSpeedTop==0)
        {
            key=0;
        }
        break;
    case KEY_A:
        g_fSpeedLeft = -40.f;
        g_fSpeedTop=dGetSpriteLinearVelocityY(g_sManSpriteName);
        if(key1==0)
        {
            dSetSpriteVisible(g_sRunSpriteName,1);
            dSetSpriteVisible(g_sManSpriteName,0);
        }

        break;
    case KEY_D:
        g_fSpeedRight = 40.f;
        g_fSpeedTop=dGetSpriteLinearVelocityY(g_sManSpriteName);
        if(key1==0)
        {
            dSetSpriteVisible(g_sRunSpriteName,1);
            dSetSpriteVisible(g_sManSpriteName,0);
        }
        break;
    case KEY_R:
        dSetSpritePosition(g_sManSpriteName,g_fKeepX,g_fKeepY);
        dSetSpriteVisible(g_sManSpriteName,1);
        dSetSpritePosition("tu1",g_fTu1X,g_fTu1Y);
        dSetSpritePosition("tu2",g_fTu2X,g_fTu2Y);
        dSetSpriteVisible("tu1",1);
        dSetSpriteVisible("tu2",1);
        dSetSpriteLinearVelocity("ciapple1", 0, 0);
        dSetSpriteLinearVelocity("ciapple2", 0, 0);
        dSetSpriteLinearVelocity("ciapple3", 0, 0);
        dSetSpritePosition("ciapple1",g_fApple1X,g_fApple1Y);
        dSetSpritePosition("ciapple2",g_fApple2X,g_fApple2Y);
        dSetSpritePosition("ciapple3",g_fApple3X,g_fApple3Y);
        dSetSpriteVisible(szName,0);
        key1=0;
        only1=0;
        only2=0;
        break;
    default:
        g_fSpeedTop=dGetSpriteLinearVelocityY(g_sManSpriteName);
        break;
    }
    dSetSpriteVisible("start1",0);
    dSetSpriteVisible("start2",0);
    dSetSpriteVisible("start3",0);
    g_fManX=dGetSpritePositionX(g_sManSpriteName);
    g_fManY=dGetSpritePositionY(g_sManSpriteName);
    dSetSpriteLinearVelocity(g_sManSpriteName, g_fSpeedLeft + g_fSpeedRight, g_fSpeedTop);
    dSetSpritePosition(g_sManSpriteName,g_fManX,g_fManY);

    dSetSpriteLinearVelocity(g_sRunSpriteName, g_fSpeedLeft + g_fSpeedRight, g_fSpeedTop);
    dSetSpritePosition(g_sRunSpriteName, g_fManX, g_fManY);

    OnKeyDown(iKey, iAltPress, iShiftPress, iCtrlPress);
    running();
}
//==========================================================================
//
// 引擎捕捉键盘弹起消息后，将调用到本函数
// 参数 iKey：弹起的键，值见 enum KeyCodes 宏定义
//
void dOnKeyUp( const int iKey )
{
    // 可以在此添加游戏需要的响应函数
    OnKeyUp(iKey);
    switch(iKey)
    {
    case KEY_J:
        g_fSpeedTop = 0.f;
        break;
    case KEY_A:
        g_fSpeedLeft = 0.f;
        if(key1==0)
        {
            dSetSpriteVisible(g_sRunSpriteName,0);
            dSetSpriteVisible(g_sManSpriteName,1);

        }
        break;
    case KEY_D:
        g_fSpeedRight = 0.f;
        if(key1==0)
        {
            dSetSpriteVisible(g_sRunSpriteName,0);
            dSetSpriteVisible(g_sManSpriteName,1);

        }
        break;
    }
    g_fSpeedTop=dGetSpriteLinearVelocityY(g_sManSpriteName);
    dSetSpriteLinearVelocity(g_sManSpriteName, g_fSpeedLeft + g_fSpeedRight, g_fSpeedTop);
    running();
}

//===========================================================================
//
// 引擎捕捉到精灵与精灵碰撞之后，调用此函数
// 精灵之间要产生碰撞，必须在编辑器或者代码里设置精灵发送及接受碰撞
// 参数 szSrcName：发起碰撞的精灵名字
// 参数 szTarName：被碰撞的精灵名字
//
void	OnMyKingColOther(const char* szMan, const char* szOtherName)
{
    if(strstr(szOtherName, "ci") != NULL)
    {
        g_fDManX=dGetSpritePositionX(szMan);
        g_fDManY=dGetSpritePositionY(szMan);
        dSetSpriteVisible(g_sManSpriteName,0);
        dSetSpriteVisible(g_sRunSpriteName,0);
        if(key1==0)
        {
            sprintf(szName, "x%d", i);
            dCloneSprite("x", szName);
            dSetSpritePosition(szName,g_fDManX,g_fDManY);
            i++;
            dSetTextValue("diem",i);
            key1=1;
            PlaySound("game/data/audio/die.wav",0,1);
        }
    }
}
void dOnSpriteColSprite( const char *szSrcName, const char *szTarName )
{
    // 可以在此添加游戏需要的响应函数
    OnSpriteColSprite(szSrcName, szTarName);
    OnMyKingColOther(g_sManSpriteName,szTarName);
    if(strstr(szSrcName, g_sManSpriteName))
        if(strstr(szTarName, "gg") != NULL)
        {
            dDeleteSprite(szTarName);
            for(int a=0; a<i; a++)
            {
                sprintf(szName, "x%d", a);
                dSetSpriteVisible(szName,1);
            }
            dSetSpriteVisible("end1",1);
            dSetSpriteVisible("end2",1);
            dSetSpriteVisible(g_sManSpriteName,0);
            dSetSpriteVisible(g_sRunSpriteName,0);
            key1=1;
            PlaySound("game/data/audio/end.wav",0,1);
        }
    if(strstr(szSrcName, g_sManSpriteName))
        if(strstr(szTarName, "unkeep") != NULL)
        {
            g_fKeepX=dGetSpritePositionX(szTarName);
            g_fKeepY=dGetSpritePositionY(szTarName);
            dSetSpriteVisible(szTarName,0);

        }
    if(strstr(szSrcName, g_sManSpriteName))
        if(strstr(szTarName, "cao") != NULL)
            key=0;
    if(strstr(szSrcName, g_sManSpriteName))
        if(strstr(szTarName, "di") != NULL)
            key=0;
    if(strstr(szSrcName, g_sManSpriteName))
        if(strstr(szTarName, "tu") != NULL)
        {
            dSetSpriteVisible(szTarName,0);
            dSetSpritePosition("tu1",g_fTu1X-1000,g_fTu1Y-1000);
            dSetSpritePosition("tu2",g_fTu2X-1000,g_fTu2Y-1000);
            PlaySound("game/data/audio/apple.wav",0,1);
        }
    if(strstr(szSrcName, g_sManSpriteName))
        if(strstr(szTarName, "cao1") != NULL)
        {
            dSetSpriteLinearVelocity("ciapple1", -100, 0);
            if(only2==0)
            {
                PlaySound("game/data/audio/apple.wav",0,1);
                only2=1;
            }
        }

    if(strstr(szSrcName, g_sManSpriteName))
        if(strstr(szTarName, "cao2") != NULL)
        {

            dSetSpriteLinearVelocity("ciapple2", 0, 50);
            if(only==0)
            {
                PlaySound("game/data/audio/apple.wav",0,1);
                only=1;
            }
        }
    if(strstr(szSrcName, g_sManSpriteName))
        if(strstr(szTarName, "cao3") != NULL)
        {
            dSetSpriteLinearVelocity("ciapple3", 0, 50);
            if(only1==0)
            {
                PlaySound("game/data/audio/apple.wav",0,1);
                only1=1;
            }

        }

}

//===========================================================================
//
// 引擎捕捉到精灵与世界边界碰撞之后，调用此函数.
// 精灵之间要产生碰撞，必须在编辑器或者代码里设置精灵的世界边界限制
// 参数 szName：碰撞到边界的精灵名字
// 参数 iColSide：碰撞到的边界 0 左边，1 右边，2 上边，3 下边
//
void dOnSpriteColWorldLimit( const char *szName, const int iColSide )
{
    // 可以在此添加游戏需要的响应函数
    OnSpriteColWorldLimit(szName, iColSide);
    if(strcmp(szName, g_sManSpriteName) == 0 )
        dSetSpriteLinearVelocity(g_sManSpriteName, 0, 0);
}


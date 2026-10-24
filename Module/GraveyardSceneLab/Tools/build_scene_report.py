"""Build the third revision report from actual GPU captures (Pillow required)."""
from pathlib import Path
from PIL import Image,ImageStat
import json,re,html
MOD=Path(__file__).resolve().parents[1]
REPORT=MOD/'Report'
CAP=MOD/'Captures/lighting-v3'
OUT=REPORT/'images/scene-v3'
OUT.mkdir(parents=True,exist_ok=True)
for p in CAP.glob('*.png'):
    Image.open(p).convert('RGB').save(OUT/(p.stem+'.webp'),lossless=True)
for view in ['water','scene']:
    frames=[Image.open(p).convert('RGB').resize((960,540),Image.Resampling.NEAREST) for p in sorted((MOD/'Captures/motion-v3'/view).glob('frame-*.png'))]
    if frames:frames[0].save(OUT/(view+'-motion.webp'),save_all=True,append_images=frames[1:],duration=83,loop=0,lossless=True)
for p in (MOD/'Captures/stability-v3').glob('*.png'):
    Image.open(p).convert('RGB').save(OUT/('stability-'+p.stem+'.webp'),lossless=True)
css=re.search(r'<style>(.*?)</style>',(REPORT/'foundation.html').read_text(encoding='utf8'),re.S).group(1)
css+='\n.metric{font-size:32px;line-height:1.25;color:var(--gold)}.smallfig img{max-height:450px;object-fit:contain}.trio img{aspect-ratio:16/9;object-fit:cover}.diagram text{font-family:Microsoft YaHei,sans-serif}.hero h1{max-width:1100px}\n'
foundation=REPORT/'foundation.html'
foundation.write_text(foundation.read_text(encoding='utf8').replace('第二轮光影改进报告','第三轮场景改进报告'),encoding='utf8')
archive=REPORT/'lighting-v2.html'
old_report=archive.read_text(encoding='utf8')
if '本页为第二轮历史报告' not in old_report:
    old_report=old_report.replace('<main>','<main><div class="callout"><p>本页为第二轮历史报告，图片与参数保持当时版本。<a href="index.html">第三轮当前报告</a>包含后续的亮度、稳定性与水面改进。旧版 GPU 与性能数据请查看 <a href="../Evidence/validation-v2.json">第二轮验证快照</a>；本页直接链接的代码已随新版本更新。</p></div>',1)
old_report=old_report.replace('../Evidence/validation.json','../Evidence/validation-v2.json')
archive.write_text(old_report,encoding='utf8')
def fig(name,caption,cls=''):
    return f'<figure class="{cls}"><img loading="lazy" src="images/scene-v3/{name}.webp" alt="{html.escape(caption)}"><figcaption>{caption}</figcaption></figure>'
def compare(a,b,left,right,caption,old=False):
    prefix='images/lighting-v2/' if old else 'images/scene-v3/'
    return f'''<figure><div class="compare"><img loading="lazy" src="{prefix}{a}.webp" alt="{left}"><img loading="lazy" class="after" src="images/scene-v3/{b}.webp" alt="{right}"><div class="divider"></div><span class="label left">{left}</span><span class="label right">{right}</span><input type="range" min="0" max="100" value="50" aria-label="{left}与{right}对比位置"></div><figcaption>{caption} <span class="hint">拖动分界线比较。</span></figcaption></figure>'''
gpu=json.loads((MOD/'Evidence/gpu-validation.json').read_text())
perf=json.loads((MOD/'Evidence/performance-validation.json').read_text())
temporal=json.loads((MOD/'Captures/stability-v3/temporal-validation.json').read_text())
def brightness(p):
    rgb=ImageStat.Stat(Image.open(p).convert('RGB')).mean
    return sum(x*w for x,w in zip(rgb,[.2126,.7152,.0722]))/255
old=brightness(MOD/'Captures/lighting-v2/overview-day.png');new=brightness(CAP/'overview-day.png')
doc=f'''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Graveyard Keeper 2 · 第三轮：明亮、稳定、有深度</title><style>{css}</style></head><body>
<header><nav><b>SCENE STUDY / REVISION 03</b><a href="#day">晴天亮度</a><a href="#stability">消除闪烁</a><a href="#water">河流深度</a><a href="#reflection">折射与反射</a><a href="#motion">连续画面</a><a href="#run">运行</a><a href="#evidence">证据</a></nav></header><main>
<div class="hero"><div class="eyebrow">Graveyard Keeper 2 · scene revision 03 · 2026-10-20</div><h1>晴天更明亮，阴影更稳定<br><small>真实河床 · 水深与折射 · 景物倒影</small></h1>
<p class="lead">这一轮针对上一版的四个实际问题：户外压暗、风中投影错时、共面瓦片争抢深度，以及只有颜色水纹却看不见水下空间。</p>
<div class="facts"><span>ApplicationWindow + Render</span><span>同帧风动与阴影</span><span>稳定地表层序</span><span>独立水面通道</span><span>真实场景反射</span></div>
{fig('overview-day','第三轮晴天总览。原树木、建筑与像素素材保留；天空补光、晴天云量、地表铺贴和水面结构已调整。这是新模块的真实 GPU 截图。')}
{compare('overview-day','overview-day','第二轮：整体压暗','第三轮：晴天与水面','固定相机的第二轮与第三轮对照。它是多项修正后的整体结果，不能用来单独归因某一个效果。',True)}
<p class="sub">水、风、落叶和可行走坡道仍在同一个场景中。<a href="lighting-v2.html">第二轮原件光照调查</a>与<a href="foundation.html">第一轮基础报告</a>保留为历史记录，当前实现以本页为准。</p></div>

<section id="day"><div class="num">01 / A CLEAR DAY</div><h2>亮度问题来自照明的组合，不只是曝光</h2>
<p>复核上一版画面：白天大部分像素落在较低显示亮度范围；整片云影又令同一总览的平均亮度下降约 14.3%。接触 AO 的平均影响很小，旧后处理反而在提亮，所以继续抬后期并不能修正受光关系。</p>
<div class="grid"><div class="card"><h3>天空补光保住暗面</h3><p>保留原太阳与反向背光的色彩关系，提高显式环境校准值到 1.75，并加入少量偏冷的天空补光。树冠背面仍有颜色，亮部与暗部更容易分开。</p><p>云影主要削弱太阳直射，天空底光保留大部分；默认密度 .47 代表此预览的晴天，不再拿材质中的 .643 当成正在运行的天气。</p></div><div class="card"><div class="metric">{old:.3f} → {new:.3f}</div><p>同一 1280×720 总览的平均显示亮度（RGB 码值加权，范围 0–1）。这是版本对比，不是物理照度，也不是原游戏截图的匹配分数。</p><p class="sub">环境光校准值是迁移参数。原 Unity native 常量上传比例仍未抓帧确证，不能把它解释成已恢复的原作准确倍数。</p></div></div>
<h3>土路不再用裂黏土小块密铺</h3><p>原草图与正式 TerrainAtlas 中的图块非常接近，不能因为路径含 <code>_TEMP</code> 就认定整张拿错。真正调整的是道路裁块、铺贴尺度和变化：从正式图集按 48px tile 边界提取 192² 草地、干草、林下草与细土，按世界位置做宽缓混合。</p>
<p>图块保持原像素、原颜色和透明度，没有放大后重新画一张“更亮”的贴图。重复周期统一为 192 个世界单位，替换原来 56px 土块以 96 单位反复铺开的细密裂纹。</p>
<div class="trio">{fig('overview-day','晴天：提高暗面可读性，保留投影方向。')}{fig('overview-dusk','黄昏：主光与背光的冷暖仍分别变化。')}{fig('overview-night','夜间：保留蓝色环境与暖灯，避免白天校准变成全天统一曝光。')}</div>
<div class="source"><a href="../Assets/terrain-v3-manifest.json">图集区域、原瓦片引用与哈希</a> · <a href="../Evidence/scene-v3/lighting-v3/outdoor-review.md">户外亮度复核</a> · <a href="../Evidence/scene-v3/visual-measurements.json">版本亮度与高光统计</a>。</div></section>

<section id="stability"><div class="num">02 / FIX THE CAUSE OF FLICKER</div><h2>投影与树同一时刻，瓦片也要有明确层序</h2>
<div class="grid"><div><h3>问题一：影子在追赶树</h3><p>旧版为了节省提交，把影图限制在 12Hz，但主视图的树木每帧变形。两张图用的不是同一姿态，树冠会照到自己的“过去位置”，边缘显得抽动。</p><p>现在把太阳投影按素材合批，保留每棵树自己的风相位与幅度；需要更新时使用当前帧时间。云影也取消整块跳步更新。PCF 改成先比较深度，再连续插值可见度，缓和跨越影图纹素时的阶跃。</p></div><div><h3>问题二：两个面抢同一个深度</h3><p>部分原模型的地贴、水平底面、崖帽与新地表恰好共面。深度相等时，绘制次序和数值误差会让不同瓦片交替露出。</p><p>为这些面分配稳定的微小投影深度层，保持屏幕位置和世界高度；正常裁切材质在通过 Alpha 门限后作为实心物体写深度，粒子和光晕才使用透明混合。</p></div></div>
{compare('pcf-off','pcf-on','单点深度比较','连续可见度过滤','阴影边缘比较。过滤让移动时的覆盖率连续变化；同帧姿态和稳定深度层则修正错时与共面问题。')}
<div class="grid">{fig('stability-frozen-first','冻结风、水、落叶与云影后的起始帧。')}{fig('stability-frozen-last','经过多次绘制与相同像素对齐相机后的末帧；程序比较实际 GPU 像素。')}</div>
<table><thead><tr><th>检查</th><th>本轮结果</th><th>它证明什么</th></tr></thead><tbody><tr><td>冻结效果并重复渲染</td><td>{temporal.get('frozen_frames',13)} 帧；最大变化 {temporal.get('frozen_changed_pixels_max',-1)} 像素</td><td>静止输入不会自行出现交替变化。</td></tr><tr><td>60 个连续风动时间样本</td><td>投影时差 {temporal.get('shadow_time_lag_seconds_max',-1)} 秒；云影时差 {temporal.get('cloud_time_lag_seconds_max',-1)} 秒</td><td>投影与接收端使用相同帧的时间。</td></tr><tr><td>整像素镜头平移与反序提交</td><td>见完整稳定性记录</td><td>检查瓦片深度与次序依赖；平移比较仍可能含采样边缘和后期差异，不能据此承诺所有像素恒定。</td></tr></tbody></table>
<div class="source"><a href="../Captures/stability-v3/temporal-validation.json">连续帧与反序提交诊断</a> · <a href="../Assets/shaders/scene.vert">风动与稳定深度层</a> · <a href="../Assets/shaders/scene.frag">连续阴影比较</a>。</div></section>

<section id="water"><div class="num">03 / WATER NEEDS SOMETHING UNDER IT</div><h2>河床先真正沉下去，水面才有深浅</h2>
<p>上一版在地表 Shader 中直接把河流区域涂成水色，下面没有可透视的河床。本轮沿河建立截面网格：岸边接到地面，向中心下凹，最大深度 25 个演示世界单位；上面再放一张独立的水面。两块嵌入浅水的岸石和一处小浅滩让水深、岸泡与倒影有可观察的空间关系；这些位置是新场景布置。</p>
<div class="diagram"><svg viewBox="0 0 1050 280" role="img" aria-label="独立水面与下凹河床截面"><path d="M20 85H180L285 192Q525 260 765 192L870 85H1030" fill="#544f36" stroke="#c5b17a" stroke-width="3"/><path d="M180 85H870L765 192Q525 260 285 192Z" fill="#3d9ea8" opacity=".35"/><path d="M180 85H870" stroke="#a0d8d5" stroke-width="4"/><path d="M540 88V213M532 96L540 88L548 96M532 205L540 213L548 205" stroke="#e4d792" fill="none" stroke-width="2"/><g fill="#ecede1" font-size="18"><text x="385" y="53">独立水面：真实深度测试</text><text x="558" y="156">水深由场景深度计算</text><text x="380" y="266">下凹的河床网格与原土石纹理</text><text x="34" y="56">岸</text><text x="938" y="56">岸</text></g></svg></div>
<p>先把不含水的场景绘制到颜色与 Depth32F 目标，再复制颜色和深度到最终场景。水面读取下面的真实位置，浅处保留河床细节，深处让红、绿、蓝以不同速度衰减，形成更深的青蓝色。</p>
<div class="grid">{fig('opaque-only','不含水面的真实 Opaque 颜色读回：下凹的河床、浅滩和嵌入岸石先作为几何绘制。')}{fig('water-depth-debug','水深诊断：浅处偏黄、深处偏蓝。颜色来自真实场景深度反解，并非按屏幕位置画的渐变。')}</div>
{compare('water-depth-off','water-depth-on','固定颜色厚度','真实深度衰减','水深着色单项对比。两张图均有真实河床；关闭时只让着色使用固定光学厚度，方便分辨深度项的作用。')}
<p class="sub">原 High 水 Shader 确实读取场景深度和抓屏颜色，以 D1–D4 颜色与 Alpha 分段混合；最浅层 Alpha 为 0，保留场景颜色。这里的 RGB 指数吸收是本轮迁移实现，不能称为原作完整公式。原有效 GeneralHeight 为 .05，已修正先前把残留 .2 字段当成有效值的记录。</p></section>

<section id="reflection"><div class="num">04 / THE SURFACE BENDS AND REFLECTS LIGHT</div><h2>折射看水下，反射看水面上方</h2>
<div class="grid"><div><h3>波面法线让折射随水纹变化</h3><p>保留原绿色噪声主波与两路反向次波，对连续高度场求导，得到水面朝向。视线进入水时折弯，再去不含水的场景颜色中采样；采样的深度会做检查，避免把岸上人物或桥面错误拖进水里。</p><p>岸泡跟随真实接触深度，浅床只加克制的焦散亮纹，不再用大片等亮水纹盖住整条河。</p></div><div><h3>景物倒影来自第二次真实绘制</h3><p>用镜像投影绘制 640×360 景物反射图，世界位置与照明保持原值，只改变观察投影；水下几何被裁掉。反射图为空的地方用天空色，水面按法线扰动采样，再用 Fresnel 权重与水下透射混合。</p><p>桥、人物和岸石仍由真实深度挡住水面；不是把一张树影贴纸铺到河上。</p></div></div>
{compare('water-refraction-off','water-refraction-on','关闭折射位移','波面折射','折射开关对照。水下颜色来自真实河床与场景颜色目标。')}
{compare('water-reflection-off','water-reflection-on','关闭水面反射','开启景物与天空反射','反射开关对照。可在程序里暂停、放大并观察树木在水面的倒影。')}
{fig('reflection-only','反射通道真实读回，背景以零 Alpha 清除。此图是镜像投影下的景物，不是主视图上下翻转后的截图。','smallfig')}
<div class="callout"><p>原 Shader 的反射来源是 Unity 环境立方体探针。此次平面景物反射、按 Snell 定律的屏幕折射和 RGB 指数吸收，是为新场景增加的迁移方案；原波形、深度输入、法线与反射探针路径则有 DXBC 依据。原 High 水材质也不等于已证实的运行时质量档。</p></div>
<div class="source"><a href="../Evidence/scene-v3/water-v3/findings.md">原水 Shader 的深度、折射与反射分析</a> · <a href="../Assets/shaders/water.glsl">当前完整水面实现</a> · <a href="../Assets/shaders/copy.frag">颜色与真实深度复制</a>。</div></section>

<section id="motion"><div class="num">05 / CHECK MOTION, NOT ONLY STILL FRAMES</div><h2>连续看两秒，才能看出水和树是否协调</h2>
{fig('water-motion','实际 GPU 连续时间采样：24 帧、12 FPS、两秒循环。水下纹理、岸泡、波面受光与倒影共同变化；短片接缝来自重复播放。')}
{fig('scene-motion','实际 GPU 连续时间采样：风、落叶和投影一起变化。短片按固定时间采样生成，12 FPS 是展示帧率，不是程序性能测试。')}
<p>连续捕获与验证分别保留参数：<a href="../Captures/motion-v3/water/parameters.json">水面短片</a>、<a href="../Captures/motion-v3/scene/parameters.json">风动短片</a>、<a href="../Captures/stability-v3/temporal-validation.json">稳定性样本</a>。</p></section>

<section id="run"><div class="num">06 / RUN AND COMPARE</div><h2>启动后可逐项关掉，直接比较</h2><p>双击 <a href="../Run.cmd">Run.cmd</a>。<kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> 或方向键移动；沿中央坡道上高地。左侧新增水深、折射与平面反射开关，原有暂停、日夜、风、云密度、PCF 和放大镜继续保留。</p>
{fig('ui','当前程序与工具面板。场景资源、太阳影图、云图、Opaque 目标、水面与反射均经 OpenglLearn Render；ApplicationWindow 负责窗口。')}
<pre>cmake --build build --target graveyard_scene_lab --parallel 4
.\\Module\\GraveyardSceneLab\\Run.cmd --smoke-test
.\\bin\\graveyard_scene_lab.exe --capture-lighting Module\\GraveyardSceneLab\\Captures\\lighting-v3
.\\bin\\graveyard_scene_lab.exe --capture-stability Module\\GraveyardSceneLab\\Captures\\stability-v3</pre>
<div class="grid"><div class="card"><div class="metric">{perf['mean_fps']:.1f} FPS</div><p>本机核显实测；平均帧时 {perf['mean_frame_ms']:.1f} ms。60 帧预热后测量 120 帧，包含编辑器、场景和呈现。</p></div><div class="card"><h3>仍有实际限制</h3><p>Intel RaptorLake-S Mobile，1280×720。新增颜色/深度与反射通道增加成本；影图合批抵消部分提交开销，但这仍是技术场景，不能据单机数值保证其他设备流畅。</p></div></div></section>

<section id="evidence"><div class="num">07 / EVIDENCE AND LIMITS</div><h2>本轮验证了修正，但不宣称完整复刻</h2>
<p>正常构建与 24 组 GPU 状态检查通过，GL error=0；行走坡道、崖边高度限制、人物前后遮挡与反绘制顺序的深度检查继续保留。新增的水面开关对比和连续时间验证使用同一条正式渲染路径。</p>
<table><thead><tr><th>证据</th><th>内容</th></tr></thead><tbody><tr><td>原件与素材</td><td><a href="../Evidence/scene-v3/package-manifest.json">本轮研究包与来源哈希</a>；<a href="../Assets/terrain-v3-manifest.json">地形图块精确区域</a>；<a href="../Evidence/lighting-v2/findings.md">上一轮原照明模型</a>。</td></tr><tr><td>真实程序结果</td><td><a href="../Evidence/gpu-validation.json">GPU 检查</a>；<a href="../Evidence/performance-validation.json">性能测量</a>；<a href="../Captures/lighting-v3/capture-manifest.json">本轮截图参数</a>；<a href="../Evidence/validation.json">当前交付检查</a>。</td></tr><tr><td>报告检查</td><td><a href="../Evidence/report-check.json">本地链接、图像解码和 GPU 图一致性</a>；<a href="../Private/main.cpp">RHI 管线与稳定性验证实现</a>。</td></tr></tbody></table>
<p class="sub">视觉参考仍为 <a href="https://www.gog.com/en/game/graveyard_keeper_2">GOG 二代官方实机截图</a>及<a href="https://www.graveyardkeeper2.com/">官网</a>；本机游戏原件只读，没有启动游戏、修改安装目录或抓取最终运行时 GPU 常量。地图依然是重编排场景，不包含完整原地图、全天 14 节点曲线、Unity 级联阴影、完整 High 水的全部变体或原作完整后期。</p></section>
<div class="footer">第三轮场景改进 · <a href="lighting-v2.html">第二轮历史报告</a> · <a href="foundation.html">第一轮基础报告</a></div></main><script>document.querySelectorAll('.compare input').forEach(i=>i.addEventListener('input',()=>i.parentElement.style.setProperty('--pos',i.value+'%')));</script></body></html>'''
(REPORT/'index.html').write_text(doc,encoding='utf8')
print(json.dumps({'staticGPUFigures':len(list(CAP.glob('*.png'))),'outdoorMeanDisplayLuma':[old,new],'reportCharacters':len(doc)},ensure_ascii=False))

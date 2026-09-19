#!/usr/bin/env python3
"""Install the visual terrain demo and hardened generic frame pipeline.

Prerequisites: apply_rhi_frame_patch.py, apply_render_pipeline_terrain_patch_v2.py,
and apply_terrain_upload_mutex_hotfix_v3.py.  The installer is transactional,
line-ending tolerant, safe to run repeatedly, and refuses unknown source files.
"""

from __future__ import print_function

import base64
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import zlib

VERSION = "2026-09-19-terrain-render-demo-v4"
PAYLOAD_SHA256 = '6e9f716e12b6a910032a0e5375a85a4818bd7688260a64ff21f65d2294b298c0'
EXPECTED_BASE_HASHES = {'Module/CMakeLists.txt': ['c42fa4e45dcb10ff0bcec55073d694cec1f0880136ffc0cc97107b59fb547bab'], 'Module/ApplicationWindow/public/window.h': ['b60f22cb7ecc8485fe75b9681e3026ef55af7df2f00615b3a7136e8af088d419'], 'Module/Render/Private/rhi_device.h': ['3b35d98dc587c617851ccfbf01b27ab8f8eb421314e60f40a932d1ea89394f49'], 'Module/Render/Public/Pipeline/render_pipeline.h': ['0bccbdb77c917869d81e53d2a2cdd59567a3fcf0ee614543203cc8e75ea80661'], 'Module/Render/Systems/render_pipeline_system.h': ['45dff51eee196376e8cb9c25281f9605c3f699f454e693b63a4ac3bda6ce4eea'], 'Module/Render/Systems/callback_system.h': ['75b159c2392e17f2572e1a87ab2e3bf9f846edc237f9529c4f2dd1b645b5945b', '3a659699593f5947bced0192add574512e5068256e581e772d1c60c1930501cc'], 'Module/Render/Public/RHICommand/FrameCommand/rhi_frame_command_buffer.h': ['83842f22503a123a6b4b3c3869ad0104d29df0a507c595fa12c6f158d256a480'], 'Module/Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h': ['7e09a38d5dbae31671428c1fb76648b457eacd48273362fd9decf1e8261dfba0'], 'Module/Render/Public/RHICommand/rhi_command.h': ['dd75815a4acec75d955b2866be32f002cfbff6949a734b4f4db1af2501d1715a'], 'Module/Render/README_RHI_FRAME.md': ['fd2ab7ce4f7477b3f4cfa3e47697e022be500ecdd42ccf151df2fef61107b7bf'], 'Module/Render/Test/frame_command_buffer_test.cpp': ['c26846330f4d6ed5b1cf0072632a2a9dd05e7c719dc4fdc6e1f4cc245c848621'], 'Module/Render/Test/test2.cpp': ['7cc43ff83d3a8bccd51f9d853d31847d3c43fc0a15841cf209523e79631142dc'], 'Module/Render/Test/test1.cpp': ['730e5d0c4a7b68c401c91988bcc2778943fa72537506365c1649074ce6f9ece1'], 'Module/Terrain/Public/terrain_components.h': ['94a358e3acedfc01a6510a3fa020dae79fc8f1b08347bfc7ad3ca390317ac9f0'], 'Module/Terrain/Public/terrain_mesher.h': ['540d344e22b5e04bec7501a592f5527e393c712e473d6f189d97b36440fc6a74'], 'Module/Terrain/Test/terrain_pipeline_test.cpp': ['1057571805a7c8fcf3f92e4fca6e6c5aad63bcbc7e5870baf3afdfec30b922b5'], 'Module/Render/Public/Camera/fly_camera.h': [], 'Module/Render/Systems/fly_camera_system.h': [], 'Module/Terrain/Test/terrain_render_demo.cpp': []}
PAYLOAD = (
    'c-rl~`Fk73u_*elw3PSw00{&a$+8SC{$!Gpc_Uf6BxN68%Lg6;Lt-KjgUk%LnCAQJB;FS}iJi@M9OpQ(PwqLfvz*wGy!<ck3Z(Q?'
    '{)JoB+w?Yrg|wZU7k{5c%uIK6b#--'
    'hRd;pO?(s`uz0>r^<}P{H{fj}I#N}i=IbJ#L)$3J%+pl#JZ?)+c5<gBRuC)mK_kZoqT{`#eh3eeZrKN?3R;tUFuP)6kROc6$_Qsa'
    '{R^5-rR-p74R+%i<+U;wt;<eVbR^mnLeo_ruwPvU8SL;FK*OD*_{Fp%}SC$qZKDV-'
    '<x1vJlRkI5$wAO=`KWMozpFIv*ekBZ>=UVl1&2{KS(%8rWz^L5o``Ed5yBXBHBnVrN1g(0wH8w<*E1gw%Gfc%O*z}U#Sh7Lxvsl0'
    'Ah|&TFm1UYi>-sSK(hOF|>i%kH-'
    '2o21MP3%Lunj}pJT=Ll)|+cvr^HXM&Vd;dBx(fJwaDA>tF>@r!)w*6tDUtqKdK`5!6r^7nEobHE+*gfBs=UJJM<JAj(J@pp7>GZ1'
    '+8j3X#2oYexD<~!m0@w>43q8xd)xFU_ADnaJ3rm#EHK#RHbTMYxvmT0czL6R^o3Df}|JK8p%!@xYUnpQP4(_GfefM6(?S+<_}XB='
    '7G2FN5fQYM}9N(>ccc2*ZkH%v~6@iH0gs<?6Gn>80a5=k^@nKwZcP}SI8ra51pDUvKm?PJ%>c3oirlfs{;g>@c@&~^ly<xVDiYTC'
    'Bddo*zy>UU4~WR!bK8$8*LaILcZ<$Z4x9T_S;?rixydn!VQu%e9{1jO+StilDqjhBy)?T=Cue&l&BWOK3SMs9^3HaM$lR(TR?`el'
    'Ms*8gSg%FcFMHFYG*T~QA7n7Otp=Wl^aSPDNs`O9a<tCUMdYInT|j@9acgeQc4|_q#8zA4K1;-qCO+@B$vW!y4>nZb`2uGhLB-'
    'LA;<brSO(SAhpVOO;)gR22GFtwHxmXl6>NTU7z{%)n_@y_G-EF`xsB+ig<)Q#CI)Xzu1UvB$1?|}O-'
    'VPlMW4zIFy6n_j=c2^kAxru=z7K(l}y*-'
    'q#m@AS@m5=WuDj6UdBlT3z7D!lLXBm*_qWLBEDnzPq{IxzhB!LqkW?vluy@=Yhymf>9y+BuEVdY5aW$&$F)lHI1D(&#^%CS95($2'
    '{bn14elv`!&2XKSzt(Dj<rBBPnolfBkzFhRJ?qpGzN#;`{TjYTdUt>@rzWcjtiJW6L1xIr<k|5l<6FZI)*A_YymwN31CmxMj9w%O'
    'lBN$|uN_xfzjmCLTm|gOX1vp?K@mX1hxdC^h*YiV#W6|IWMf)k)!6Go5h~UFHLuf5XdV3IZ(}Qg=uScDRSf?WQ@*Ou7u5pAb|c*M'
    '3$#Fy?CuF*|F3LlH?6na=p^v6RVeaK$l|;-'
    'j8`n$A7MB){luSA3ymP(gN_tNGdblou)F}&1dS>xl2+(%*Zg*(;&utTaUPT|rBkeQ2Cga^{lE{_NFiwiF*!CvMtC{pvy(i1<EG(<'
    '|3!Y%iCW|YwrJw75)DUpgk10w`~ic-'
    'zU^9o=y;oeC<`ve=TNl;=~>UIAEc!$Fc&VKe}oB~6Nt$}9%{;xeX=038g86KTRQjQ2&i~i^yiTIWOC9KVH`tAwAc!gz-'
    'tCix&UdV3PtO?(%72UjD2IcwHAd~R~t-cyt4`xXY_jzL`?zdTb;Z7(8|KUtW+OfSXy4Z{807Mx&L-~sYFhz9Mo66#fPxoJ-zB(zO'
    'wMpg^SfIOP9|tUR)><_2t~<r3LY&DCg?8fNCI;K>4W`s?~E>u3TKGR*QYLa{lttBj=Xp0q~_O=T>-'
    'QD@#`w1eV{Ti=Rp(&OSg%ggXazTOxQ)r#+~@Nl>fSyf~T0)sBj~YMILQVyUmPOwL<{={G9m%e5-'
    'V{DN7c)oC`{N#uT;zD!9dcu#h2MwHItb(8%pKEl2HIkt+<bs|_^69L`ifaNW(O-'
    '<cRugN>kmZ$s41gse2W_br>fvnykWy;!&Z_ioTh0QRiD>Bs-'
    '1%gP&vWbLf&Vy|dg*%)#9DUchiG#|NA9`n0^6MgO8Ep1ueXbeCzDo!WHp+Rzf)`0=m)f$?U7fU7!e(<}6UHspiHo4rZ)Rn>hzUh;'
    'n(1$q%4Mp|KEM<=&KI+|9<T!Z&AFJBvxoDTM%t&IBK?zxB7|iCsVp+7i&V{PLp{IFRje6hElMlo^^0)5RI4C#Rkn##U4iMLbsgGy'
    'OmF$|f|Fw%6HBjWaxY)5y!u73_%B?<fBmQc6879Y?g6x;uvSFCFc??h_qlpK^5fXOtX;f(p}MfNba|<8?YP$UYsX8V$*y@p)31{x'
    'BnX)qD925F(xK_KFnS$;wCP7N@Ddf}sL4?68L$^?mb1IO(lC8!g`~#lO87P&AG*Wz1lwGE==^0@laU{wTx|m+wze>FP-'
    '}S6iP?haiKYwYan`(_c(sPbTm5sBBBuLl5Lbr2xfcsRKo6PX9JbZl<MTM3Rg%PQX4qF0u_cx1ezbPFjsfn5;$5ciRfN(NfECcyQ-'
    '|nh#SFbzvF%kT&UM8G;95|00vINpKu;BJSxwSaDWf&BnV^Nc(_9^5H61FfsLjT5Pu-'
    'Z%w`Iog7_#nf1~tFOmIRn%8$nImI{>Dr2e{T$H@zsp{q0$E69cr)*<KqPHOD5#TBlWWY<Dz+jUb7&R~sE*OzjPu8u}~TcTFPCtnA'
    'z?xpq8U{f=KtsxU&<_Qp_qB6_8z2N&nqk?+`fJOCCC)`{>@!Qm&%hX&`jYGHj4I9OF4s0`dGtQ|LS!R%R=Gcy<Lv}V6!VpQeTdKJ'
    'eO1a+B+6vrujw!>B;y*YJPj8$V>VVxNFtWwIvFvHWQW!mmE6A#SVdf1Pk&rH<wQryzH+OFq*pQoEV+20xbtF2%yj5e|X<P=~Kmg%'
    '@Ihn=YAyS`tDymlj~v0|VZtpfwP%3a~y<wC;!jn!KLmVo8YRn7SchiyI#qXl}D2ZBletWKEtT5DF>EQEoYr}CBV7izLx9nqRPm}2'
    'pO0!w~OiF5@eeWW}nWz^Og$!X~|<+WPlGL>#ul4yF7bK@)#vqjx2Wpb1-'
    '8Mngpjj5Cz@OlNrs;G*;ap_S$2@PEZYOfi*f!e0lI2G$;3nca>{N?!84A%VGPOa%@6^J^m<s@v|pV?AY<;(6;r&Zmh3np7FwLRT*'
    'h?eLqXXC@QBKF`|q~UG5iAT+J;lW8wR%Vs8o>BH(Yo~gp6E}DRg%!aBn({_5Zicn%)%A`S)u+=A&ZZ=|2;U0^K~xorTXERcGFk_?'
    'ah9=7p)+C`wEq9z@jL$X%50%n))6mEl*ov{S=%_^16po{NwBt44O@Pp*x$}V4(U{#X9>_y*}kA@$e_VHr^N_iE1|OO6NAcBO-'
    '9h{h(q<m3Mj9gq}}1ZLdKZT%_x}OASl_+aS(%k5p04E*4%-Wp}m8wUEKt-nd87(A}49n;aXv(NM<=+<qdx$jCQK+%+X<^?{C!FJB'
    '1O3UrAJ6-'
    'AR1zLskYU?|~6;QuHfrOhreaGLwEDew0c?{=VcVjj*nA{Coh*T2gC#wPVKGB^&gQW8CBn+ubuKhs995RY^d>=ayxqnzT~bGjkS5*'
    'Q%jL&t$QH!Fy!ewAnj{;%7k!YMhvb;^^!N*4bjgIS(@jP!U5_+<5TgSX;vieT;}cCFqz4j+!Ck3Lc;ekOD6d&~NZ(TCgogd@k&?k'
    '|}cXWMEO4x4u{>fj)Bmk6r*m^{J=q9fV`4fUi_E3M~6crLqYoRlN|@dvC}XBOkPARb$+P+#Q+z4lL-'
    '%h1U&(R1U*d0JDq$%4I;H<6C&wyx1q>6&nW%(lt$|RT2SJaDiWsV6<S!nqf3suwhX=jS(0T%7zO_af+-'
    '0FJ7N=AUiQMvgeyYdkFNfS{_1^hoPRTS`SA$6+#}Grw)Ur(4B`vQPj>u@AS~<%}3tW5O`s=JcRB!6hcd&QV)p~R?S1~%+QEE;I9W'
    'Q`gI7@(#m<r-8(dL3$6OFsEMk1h@CyuEDijvb{Hi?&QM-E554;igWhs2h~r^JFRz>{a))0a9tgnpV;1x<JeSta#c$$J_-VTnLt?4'
    'H$i;Q|<$`V292Q##lw5pI9ZC)^g6X&I*M}5^yml^rrw@rAbGjWGK~_BjL+<zGz|d8C&!NeN*Nc6a0pe7KYuhuX{n&@SCN{8AtBvi'
    '9oo~&m2hq$nV4vtlKtU8EHTpx-F!9iEgnSpv!GL6KCGe(awLO8YM$q(u0JO3=-'
    'dc>&iLkT5nqJswCp!+G9}Z8d*)v1~awTkAZeR3tvjK&v8=!4f@xHmNDrv{_J?YD>^Fb?!8%`5}wo~((O>99s?6Rt7+6BwpPoG1H%'
    'Wd3yakKAZ?zZ^LVu|+|4+uBdl>`^o>4anT3apk3MapKyY{uRjh7;LFtdkSjC&r1Q7)(3rNb-LsE85H_x^rmm%j2IMP8?u3>Cou=2'
    '>d9U4UvYHElj2}&nspNisZFRsVK6M#ydG|sf%;QJM;X*%>fF~!T2Dv9fdO-z+L>oc4Da6A9rds->>^vTX4<jtlic>p4gH-'
    'A5?P5iiZ$h3hyf7XFBt&Oyga<l-gkClyIG(Ip&0+*7bpJPr#01ePOJhn>lcmihUng<^a<Q^o5z@u+f2@TB<LwIcJonc4;-'
    '{s@qqi#-XO8AL-^xA9T(Gr<c$@N14o}yPJ99R@+f^i&zF@FzKp+RV1no%Oy<N+2<pmi{tsHb9-AH#-'
    '!aUvlmm2jaXG8yY^#I;&N~!V&8mpb}?S=tZoDeI`)e;nm$-'
    'J7yK4pe4svOF7c)|m$!R=V%3_rSq2Bk?y0B9G3&rsIqzFyM~l6zra?NBvwfcWGDfUj=K%_#&A7MJdkxIq-'
    ';GKoM<QLS2Bj9IO^hrRQ+G{9KASLm86LQ7Hm`CYhn)GG<p$xN+5+HUlRK*oWuBXWT{B%BQD1;666;mr{}AmOJ^Z+6E~xGoXf%~*$'
    'rIG^aNbKi4P9UsEN}QnYv*XfM@scC0Wy!c{7vMAD{Oqm>F;4tOX#Wq)8TwlX!%=2^UN)9ge+R6N-'
    'w?sBl|nm9}S~U%RxJ*8@ZE|j-g;6MqHRM@yao_FETN2j5rA+zd$9F`!qPqYn~pePEA>GDd%-'
    's0oa{YbQ{qfM$k_rb*iyj6pkBMrhuhO<gpQlJF&6}rTdlOLSjgdh8JJKOG2%6xjtEQAjjxQtJ{L9574&j3$Db+(war*-W>M5+-'
    'XsQxj?G*9kem<l-'
    '}BSI!mRWji|z4PD{E^S)NW2=bcBn*|6ltxZR+9QvBqN1%~kj#|}6wY`4|rWzXd{#p)ZkVfAm)$ACTCw^Gwj*2UC}TKtBr+Wsy<FR'
    '*jy!A122=(8ammJTTv@?5mI6B>2U(j3xEN#XR)GRkPKc;)YeL5l_zp~cNF+Rz?}6B(V2{x(n`ajI2HFM$2qUgTH(?OMZYt^0TfFK'
    'n$BMC}rFN-'
    '1V76vCGf2f`_$ct^!<0b|U<h#YqD3MD?Y)d=DeAJx8>43lY&biiFQ`>?}f41pj$Ut5a3z4G>sfZ5Co%z#P@uMEk1cImBUh25vj8`'
    'Os5>j=B7mcFpz>8eT!Z)^<rWZNJfP%)NT8UgMkc=e~nZW%Op2-66GvsZU$bRaAXLa)#YqYW$|;o-'
    '*(FKBUjSS*p2zX>IIJxy4a30c9L>DPU~9ZMuf*?zi&$Yduql|YmwezbwAB04+bD!(wdOqg>ypbh%()kT7S_+{=D$^5FWbT-'
    ';^qIbgCYQx)>@5Eebf&Lu@wJN(xG0jTO0=s$JhBD59wAo@rr~J6B<)@#gwf3yJZSE;%sLu(WUuEd?$K;NnbImTCkl(H9Cr?U5bC1'
    '-_Qo2y^@YZeB-'
    'i}txV)pu?w}XM9HkAj(Pf>=cfWctpk+w<!rp}`iXL?yk`Rv#=7j#t!tlegIjV#`~srxQxJqG%G;5XA1kyNzql=0niGfeD}7EWkb!'
    'tH=&z^_yu*h&0~K<MNQnaDW-wkn?oDPo?vx=Sv$61A1ngS7InUz=1bpB=FBDXo8#1J*v>SN|?~iH76EnL(O?+8UCl2CaQst$ikgy'
    '6*jT#?qJkxWO2D-'
    'j8c|u2<6xReVuaxllVVP(KviwWyaVof~}Oxr@!SnTHe5T>$xY8D<e}pZ55K6Q(LK(#{nSQpt7FQS2#p6ey<uw2Ou#6ze0D9hoO~P'
    'Ty*=`(cckHI9xds%$hz&|RE*kBHI%K4cZ9AE{(hORwnarYPKry>(fTJ~#@A?H%_4mZ2q^oHjDz<gHmUZi`QipjaDsZJ>0)0l{1Z)'
    '!PFOK<v^$$(#r!(gXCKqj!{`TIcASzWOHXYF*Rn_tUc!jMg#vx<vBf?wK1u=7k+=lP`Dr+6RFJGo*n~aUZ1W;j~&7FBZ>o?+s-'
    '(Uv8;|7z++IpWR){JlovmD7d(MFR%OTl-eo1hV49)dM03})N`E24@9M&=lM>lCmoE~kH2Rj_fPrtPx)2vL;cYF0x~oZtxS#*qPrp'
    'tp@q178FDA&YO1zT+KK0{Z`BW2pazYzR{LWrSy5xF)qyxFg<<fN{7@p9V84<DF^i#`i-'
    'K188(9)vEI0I;=<o%}m{M99=ajnKa+KuUd5B*3bamOyv}O9}y6SV2(Q~y(ZpYPvgY-'
    'MoM<`d4B+9yKY*Wj*j=OXWqMYh+uW8QMz3jztu--Dklu{rW^KxLpbaX3&a1_cINAWp#3=MarqhenJ?2s(0`$P2-'
    'Ria#`lyW*?9lpm-'
    'u!odT+}SQ;WS1UX<ONvBQ#@j^)_UR4H^eHh`vT_9N^BZg9{yx}E$f@Ahgs7Z4R9~_%;{=ER@E(`G+3qh&LURw;Fg5>YVh!t<b*B@'
    'F8B%W^LadbNQ837ASCS5)!0W>-'
    'c&NuKBB*wqFBJQan>e2P7i!CppvVuC?smXdG&%+B>GWM>Z)cUyHe5{qfHgRa>c}EdgnaXx!*C<b%)j?Q$+MxG1vsBhjXm2%n?!d9'
    '#o`MKkkw>_0c%xpfkhp1}qr|sUp_bG!8+bINRqL##a*;#flP;>lL}|d(DF0e`5PSX58m;da>^2Lvs&sAK_a}Pu#B|l*`x{VL@|<$'
    'K5x%+h)@6&=0NCtP_ArR=MvYy2H}*QhD~e(E$MqMI^jV?9($=9*Jy>1N7GMIM!QBDDWT_-'
    'k)(<4?$7`M$uXQPz>(=FN^Jqj!4^iyv6bD#a=mcn|V+I{JJW|+;dL)L;k(bPI64~=FFH`y<A9l(@B&98+vNxIK_Sqvn(_I7mfIy`'
    'L*C;$C0d%VODDkYyy%rNI>eB<p@8(6n@h!RfEK*bRlm_K!^A~fwugMxP-JlYBM=##|xz$;;}JNte9c<T=6nlctW99i^z{6x?!*2n'
    'IhMW7Jbr-B5x-'
    'nJf@zbV`B{dFE`rl{1hD~c3WZ8tRHRI$E+BIJY5HSg#i%6SOemyLMErlCcM28g(z@glI%{D$JfO4qJ=j7+G&t3poDr?!k7k$+Z~5'
    'OC8GYTR_k|<2wUc_2dv2=fu?mC)9}t>H_@X#@mfiIbp72k+hL1TPD>jwK26zxnyLg0h%J!6th<6etjr}mq*^V6<;IwoBVyu|IL-K'
    'crmRsC?pkuxM`*8|QPm5_1S2iRtS#yoW(jev4niv{!v}RArZJg{j?55g3yzov7AVI;t_|p}tdI_3iFU87587U~if7_wMUJqsBICt'
    'kUnvB5J{uZmE)JuFNzQVBeq!ZSDhgpuQ(vOLI=p)9@q#*Ri|}oPc=}kfm#Z!!d*!CL1JnN43|VDACBEX*wD>ZWS$k%Nw{#I7E0vY'
    'VYsKg@+Dg!J^(al|{dUq|4avtmS9G=3tXiqp?HfP~N%(DlhqcBZl#Yqd)9F`N=j3_-'
    '#w3NLyilO2#isRAE~wn(mq62J1*TGHS}WrYGUt`?{;?>{f2+J!bi~}x-QyCSm>SSx=9o?md?1WS>z-'
    '>4#zxjB6?GoCm>NIGQ9vS#-%fTjr?lJ?)WGD61^iDoI776WJ=v9(79%W0B2FCj3-'
    '*Km?5i7M$3umYE05$p1#fRgYrel;;2<KR__r@C@uWYk+8a6A0LRSW7&lr}Bx?(Q&Z^V*%A)}+nQQtkLl4*Sz205wV@WZZo5%;cm%'
    'U^uVR;zU>ljCEfFL!x(V{6fibk>QM<PhF!#X%2v=D3OR#7;y-'
    '*Q)YmBmX&HDg)5G^=KF>sR+@7GG@Y{4(Nqp6Y+?IL|tXd1D!%u^y4ads!N8HM~;cv59w;*wggO!ghi=a;bI1%d7=0uL)9-'
    'X@TfrE~=|w^ea6XunJo8ItQeJ=$Ty?Zx?Abt~9*ZuT-@1EcKn-qP^T&3Ghx90XsdSo|i_@mlNVTBMdhfjQr+K!HgJkf>7&--YH|n'
    '?-'
    '##DXEE0aV@RGv=MgLm20e7qJ~@EtLSYpd$kAEaMUH+!0;Nk3ok~S>KB&V*$3JcycBC{g{ST5>?H_e~cqa?_;~<oq!w!T7XMmB=>g'
    '5jw+r?LtL>}2#0P$oQCEb&CKg~I2IU{9Njrj_YS6N*uW=%AC+Y(9KER;Z9Z%1e4s^aRIVrq%<nG1@pxl%Nl%IQFHbe3O(XBL~SQ{'
    '*%!*Pb#r*H4&zDGAeDexMe4#&tJ6ZoPZAK(?8`a0=GUSwO|MYM(1-'
    '#**n;G&g8_z1BufXEw|6C=R!7`Q*);*~<9n1j{@pW7iO82}`Y`5ML7z@|;^kMHLY}Ma6@Q;z%)HPB6Vxl@Ri-'
    'Q4l2^5N%Xry4+e$yeP>K8R<m`K2sTa*<e>HY`<Atyv>etZOj>m0xEFoEya|{#jq-'
    '7M1^8sQTD4B<y=_q4T9PN?y2Nr=7dJ~>9V((2T$!YaPB5|awaPglPh79Bjr*?ex*&C+IzO&<Vt<zjvi_!Tac^HV}u?-'
    'HY6=4ku7sHS(AATBbS++atcqaMS+jUSUrJeRhYNrkyD=et<DCoba|`gM{y%)gJ4MT5+I&$o~`25+w+kR>&&^@cRK;{O^L{D-4c-'
    'jj*NVA6uQWU?d(Az=R_++WSruWeyJ8%0jZH{d!>vS*-'
    '<2VrOXl~V}|LR!Y|PBc&f%7Nj2etlD)uDs#jm!`R9yGHPiYS%(ANZT;0__VGaY|-^&3d-I2_L<m&FYSx>~-'
    'Q<LssC7Dsjkc>2z0)~}X20DA|um<e9wqr6$svNRr(UXgMHPuPYgIO0~C?PUUPLJPrCTj&yD@8u4M}C2pA9xL*OXW(m!&u+%&9Saf'
    'UU?_vNM&|9p3=MMCIx4sV>7&|KK71M_F>2B!V?R}1?De+^H|4<2-'
    'd}6JzG(lU=eOiD2mjun~zw~1y_$vt}U<mAkM2^y^e?L1A)?6b(l^l@@}A(`__X5qtVk5de~&(K+=dB#)rBe!wA-Ux~pXSt9*q_Ym'
    'H(bd}z7T95tg0QuYIxQx^?KlPS`bZ)C&}XEW&deJJ2;@(s?pf*Za9K6P3&?v*_+sgM=?Cm)m>h)g(}0gEr%bgLsB7*`hXBjtFRL|'
    '4gllsoU`oCHiS>U^JvEN^Gf+0};(QX^JoJGk{i%-'
    'Ne6Yie7r$_}>fHJ|e)W`2Li#>oB~UmSe?`|b}vzkTa{{bNpgI*neczb>Q7iWCN=0Y$EwYc4-'
    's6>gk)K`S=0&bdMLNL!b&aENG=HQ~2PeKc2nBEb*=CL=KC5iKE3Ub2@7XV$O5SjnLS$%Q{l*EFJa+M<dDR;zg}^nhjieB6OGjpOM'
    'ipz1!Fv4-rq^lD^6F8?B*c8vug=jynck5K-'
    'Wnb$G~K?2fjDdC~tpB|271)O4kouMI<;s%?b2cP|<d*c(kkeW44OPK4LIYGS|uF)KqSuD$MDmarY3L=)sNKh}?#4Aqa*g<Lb#awD'
    '@ecpfb<Na5Ee>hB~G;*ef%t3Rd*u!o+d2$$CLRg<YHaB(;y}6-o2BhIg-M-'
    '%<7<4?<3r4;V`79vo`wwKpV&?Tmubtw){MDTfv%Pg279DiMHM)H@3v)S*@~HktBZXn4Z%Ki&&x8N&X2~}MMI(Brb!a^QEQzQVY=`'
    'Y?Zec39XbYe*vR|XKian2a2r_X~m^rh@8FI_<YBeQD)2sQ}c&}<yz-'
    '=RcD?>M?=sTw%r>YLC467v{+Cqk$KbA=(K?*A`XD>2o=Me6RYUE0$mu&_J;NIKxJ&f&}Gh2{gX;^g;^gZ3Ep_J>jhV##&w!R7A?w'
    'OMsl}h%m#i77Rbvgq~#zht9_&Y{h1e`zG0sltFJWCfnHpn0!-'
    '2n9uiEi{~0_pq0n^RVUPQ!$rJ7qyn)uG3NX>gUeRP|4;`l_VIKL%GF$r-'
    'J@IFoxg+ew0EknET@dvcCR4|P&`csTORIqyCYm@DT<jDd4YBC7Lf%kLzSC)_eIp!=Fv^Xh(?EG9sh<_@7rvuO%1ZI^+9qgFXonv!'
    '93QzM?n#Z?kuL_Q7BD{TU-'
    '(gq7SSVdX~K3)vf?F?}Z&Gg~2&MJAqK_rOg^2JRu<w`m)RMh5zxrfI~RmuGV0(FnFw?Ha1xJcH8J!1=2T=H>6Ue|8{IpVJm1i`vj'
    '1T|Wrzg_d&i7P~WR_pzOQM`cJwZQ!AiQ-bGeq~9f$Ei}UF%(-=>~aCN$f71?N+w=mCLJ+-'
    'Taeq|=1?2w+xHVw^1>MF7}&QRMbk4@W#@fiC26sPP|8;~rFf&<CC#|tYPG}@5W5@V9}nG*b}eIo%PTqUWu!>o6;=1LF`;)B2DsJI'
    'KNf2JU96geg}C1_$gP}G8mu~QhDo1@DXr~FKv+KqyTb+oTXn^spop&qEkj|EpTf1Z*iVe;+i8zkIG1t91^4{EYv}EY>{tR1-'
    'YVO9vni5u#z2YXx?*w!<dVk}un2ejd}0kmkW1cmfh|M5APB-*;&6*@*1-EG4pD)xN0gLMyL-'
    'b*3iX1EyB+@ZXqrQZLHeVOJ}A`5h33K-Xyr-IAF|dS6@<R-sJ#PZ=mNOQk=b*sM!lCejuZgX&en2*7KrQ+=`~<8pF_?9CiHU-gf`'
    'm;0ES+QLthG9dQvR*QSC!cZKuYHn9$czZq~`|l*>z7an56|9=n9=0h$LM?XTi}*GsHO=G9Lti`iYp-'
    'P7!GvI9;HrUjXV96;d~gOFvER~64PMnaR9Q^jhiFcFF^rFftAFa({!A9AcTgv|SU#1`JshK%U9FEI_lI5-'
    'Yimn#{4I_2`~huSgV?toFRtel`(%w~8o=7uK~vFE1pcOaG)&dpz1r~+oy^GoM0EtEIv$XVp>)4w=)<3n7(NcYnZ_CNjO{<}Zkf9*'
    'Hj8~=vHxbx-DZ-4sq!HX}DCp2(RklSCrv;WF7w?Dnv{p`*CAHUYU`DXVI-'
    '($e`KYXwI&QJf>)89j&w?BQg`^vN3*Z*htm7DbKGq*qg@xh;e1wXoPf8722*YM-w<$1FI!cX?!dHv3xKfL|vFS;+ickqW>-'
    'Cw@^x6j_X{pm{pnBmp^`G?&bzq|YP)B7*Ibm#fsbwByT!7qN$z46hV51u)A@!#)!{vZ3V{u(f1Siq+{|Mi3Yo39_-'
    '`eFC?zv%wx6@u4L2|Eo1?C6~N7TLe?`t2{?!qVUWy!+By_;!>$ks5kWkTLRvSoEIw+h@N3GTr&&uLp0w0+`>u^|L#_e;?i)eD>q+'
    'bARl9e6#z}r}+NYZ{K<Gk7N6v{GogEw_`w%{dd0H|Mb~|*PjDCx<7p3;I)q_2GEjWonj07zk2WP?_Oa>DEa@M`w^G3^e13K?!I@k'
    'd-DdM%zE1W{LStQe_?|rpeptk?LcZKVFPzQcn-'
    '?m{`9pw|MpDx#oryg^q2jopAl76BZp%1@{jjl{2`9li*MZhIZ*DI{r~*8yYKvZ|0jR#zK-t?ZhpD{-'
    'gEe#c6a}yH|~7#{ju(IKkq*K8|d!cr*G{4?6tA(OTXIx)0_LBeA0dW1<{l%`vgVT6`-Bc{)M-?w{8NjbYFY-_UEs5U-)GICm-'
    'K^?=J`6`zsRm#z%Lbei7(GCeDnVnj}!-;LpDX1_eIaf8!(IS1PN68sIv=0~4M8T226q4MO8J3&a#-gp%Nu7Y}a!6-'
    'ICWgU=Z|k;!?&UO|-'
    '$7nd&*X#Do)Un2Ye?2GPaH^>uBnx&$^HF%iP4;7yf@Ga$<cJ8GQVHn8O2QCxVPo(*$A9SC4w)^o7m^=G#{TX_Tj4WWbLl7W@VI)A'
    'K_DN}|sG*E)e|zVZ?|0w&GeBL%Y%DCeIYD><y;5Go%`<d5l$>>N>u11O9P<8)Pw&6|V)wJ}?f>U*U{>D#>{o0m!A!jK-YY;{_{!c'
    'hSA)NO_R5`)zYkFEJpJsQFW>0C^~>(-uXSJj6OPjJFYUka#qCf3y#Ld`K;eU1Kj3Y2Z~SKe-'
    '5&s|AaeHK{*UgBPhjY}FZ>GljvBduJCFzd72yLAdl?b|;P3zR0}kW;KkUExLORXUsREPr{hPPHco{!D@x&9gcKcdO-'
    '1&0v^$;2wg&tSZU*+^#uE-'
    '!wAN21`+CPH9JE0T};^`{zAdBZYLs(!>S)>MQLNtf>Sm2us{!<t)pT36%Hac^b{<&|G2RLG}H)zHzwh=lb?=LA4w<v5Z%7R<C>_Y'
    'nG>S|aLq2_qJfwY!wdZ%^986Gqx6w?cl53k^MZZbmJ^e?7`3gTe3>9hA~c#Rak+=yXS(g+z-'
    'W=XnH!b{PO5UYgP#NdULFcn%+7cp?jp#9=?YbrJLRx&D1=p|LtQPCfiv<!G<lQovUQ%vjfS`fvFU_M?leW#YhX)aSm@RI4flGZvC'
    '02TtuYp!1unSvpBfnH{VSAy4ig$QM}Y#XVRez8m*gby>7PFTkvJ%IwN`|6wf@4ScNl6gD5{dD*JUx4Vm^UND$@}4uK`=39(^V0V~'
    'Cf$Add$0l>y!Gk++f=|l`{Kc~AK>Bv0RH1Wt|NT<>D}jVfbam3+kO8708jOn*Y5uMhus@L>%Q_6c*BHT_vRZQS?+xKJYOGP_{qV`'
    '|8ej?UnUo>Toth^nHs`GqoQ~G@#hC`{fOy6w?BUy^_Jhgvj4_kx^Lgy|M^E)h?N*)`VuSHefvYE5P=YdMd|h}ki;+JLi`!5vA^nm'
    '`~&D9Xk4Ho-TCb^`+s;3lqOW|K>Q=XrwG@ke*3-dm;XhcQk6fZ{g9`i)ad9a{U0kFeE!4zUwsBW2H7f=Em-'
    '`aa=^Hl1xpbpj#vjZy?5W;|1Y5JZ^rP?^EaV5TR7OlHl~Pl9{rY8mBVuPtuJtJ-'
    'v8a*pZy7j2<kJT2Gaj;pS>W~zwy6)_9Cme|IrN?J+@u}p1AUV{(Gttd=CZ+s?%}RYfq%Lfr9V+lnr6`tuOAr^&Hd?OAPKovSLVCs'
    '>}dts5!mxJmUE3OYG(Ti+@E1qQ$zezd}?!ikuXrf9*%0x-oEfpMSOc{;Qz5-oEu0N>6nT(h{(K-'
    'udXwgI|BV|D#``#s%C(#{)`V37b3XVGFc!Fg-8g>h}~5F&m)$r$3O&+MQ3{Ir!}@)-'
    'Iw;)o_>rfG&Rh^Y6M}exE_*nmDLX`!D}H(vGs+!5i=Hzwr9)Td#Ki@XOmbUt`_i;U~eaU^nOw{&ef$*1KHg2OWL?#ScIy=zj9YgI'
    'hPc&p+3F|M~83K9_0*8w@66(g^`-<iUq8gHj4D!X)ng@WcJL-=#(foj<8cM0p4C1A7IO_nYsd>IF05)@!$K-P-'
    '@b4XA(n<_8SioiE=4F1-7*ce^kA6y^pd=)G`}Fueru?B4hxw)XaisrrKR>3N*r)Z}1L0Lkv}UjZ_J5<~0$0rYn5ns}w6U&yXh#1-'
    'Tx$o|VOih3WW@SgqC-Jkv^GnGIKx(y22voF9X9sK)EMmG2jXycrt0u7Kq_`zRLg92m~)PyR<5$EItH3-'
    '#URQnO9^$2*XPL&BzL7#czd^2d%A6dyMpFqXx0QAk*eskxwPr5gLBKI-'
    '*L~rBv4>W=L?w{TSWANb3pQG&g;!EHW=l~M)*1sM6>Y2MwzYf#>&X>;|d~k~*XIZ8zew>WCPK&`pk731ax6x2Ns(6=&h+U}$tvs)'
    '(MqfoBM}7MU-'
    '=MCKXVhsNRz+=fd%n_fnsOHw$Jeyi1uU2!HkD~}Ou<BTgMAkYuwzNjs7IG~wVJiw%l*f`)pEL5XshO@C+%;#T0U{#<akyYshLlVp'
    'B<Mtp??im`sg7s)2QW+_r#Q0Og$*w9S5zmL|ZLUuv&;x9g0B$=K&o5=s2qp8u{)7G_f}|05JC<=kDa7aNNg~yQerDlT7)hh(6zT_'
    'wopyZ=Uq1nA#myNIrK3YJTWp<?Mo**01aOr_PjY{rhxo2U!3KjBCxF<$;E)PS)(Cz97bxwqrBoY&HzF6E0Q-'
    'dBuI@NDvb*Jy;;4v-'
    'ILWNu*C1^}Z4JfP%We(GC+3y#8qHt%)?%*kMB~312)#0F@M};v~RfKv)ti@;fZ8b+)`zhKrYmkz&7a7#NwA*4ax7bE{DyH`$Zhl_'
    'zamt23i^&}@jgT_qmq;7k=COTz1ZOGeU1-'
    '>aq<Y|tqBEez^JcdJRbM&HCJiJ9kF`ABGPbUqF?I!%}ye%OiorMp%K4?~q_J_a*0+L-'
    'i<ffxk!UG7lyUZUysQzZrmUP2bH95iHDmsF;3%Zm0_Q(x_Q$)=4Bi$9KM986I44>i3>uIk}s%Kv&9?Vfj@zVqTAyFdR>SM`E0raQ'
    'JhqQ*GG(BQjfusYi#(hcjiUb7K}t<cgsz4{pbQ*O-'
    'a@7K1*@SjukgQkw2Yom7{Ws*l=Aqcm$Kvel*s_d_sVX~O=nyQS~T3lze+OJfsS8Rlc*KW&a=gQB^MdHCI<Tj0DHVF18rL5XonTC|'
    '=wB<CCO;}%aXk{6VYAWLpIGvArz%43SMI+9zRMbC$M~q@`%<L64KZ?loGz^alfUJ2z69mn*<M@phgoYd*c%u{M0XCCDwT2h*?Ru7'
    'T4%2fy_Bh^9ymq_*Yysb>59{eO<4`4{)&f6%XofX;q*x?D3*gjF5ovTxxe87w1*y^KEHx=gO%m_w!<5{Zshd{Y6xH~fNwUd{(1<S'
    'vORhJo0v~`&x49*PR5cbOuqpP#C0g|D)ravFZ2%}&ymoxbLBBOTL-$)r{}dE7kew0T-'
    'PF5#8nx4PQOMmzSweJ>0hPTo@K(a_RH>)M8a?BsnuqFBW0i}wbc@ukSL^8W)156*<1}eqsV~(mg)<gox+-'
    'w5IDQ+{tWhMRa<$N^@r;R5o+y}NA~RuQ^xH9=1!ULs*Lvq{u5nk=J80KZyJ%GH*~#;~{9rbr3Wuk(P)N%P?+#?p*%0-'
    'f3=fm@D+8*rumV#tGuU1rJNK0@nS`o<hRCn2;N-'
    ';%%rqFBeCllC&&i@Pf%v7|wPxrgf=u%)=bs|sp^4=hSTQXEvdOw=%_g_`_UglFeP-yxpL=9t68_(Wm9d1CzlU!Oln_)Bwoz%TrvO'
    'J;iBSi{bW=fxm_8%H{I#S+*0D<}miaJM$U5${!x3(ol*nqc<Lg(nRYD8z&{>kE%mz~9mPm<An*77&_(eLpe2{qw_CP1lax+ZSJ3M'
    ';j^MT*23yA@2@XgkrEFNwfh3!sh`QdfJr~6INZ5R9@XQp_;^P6+h&gjH73d!vX_h&waR%Uf~=?Buhmr4y50s`|~aw&@Q(So3Do|s'
    'uPDOVNJEa-aPM;!)7*-V-'
    '4EUO@F0#ET32)IG4=wg0RysK2ME@C~il1L^lDzE{)if=(|(th(Q`q8V&0tErF^w`Mbg&|pvF`IfZQDs7OUQoGluQ=FJ<=%0WAh~&'
    'frugNSC@T`KvX6at&6Q%eXsPiy9Y9rQpVLeZdSK>b=4bQXrlPYMHwM*ESXyr_N?P4#%c9=<fhgcuaXA@MU}kV>dqp-'
    '8brwzaPX{F(0WAdGuN~LGuq%EQf+nr|;-@gHPzCmy?S_|A&_tydOv6;OrLCH!S`H^Z7`4<-'
    'EU&QM%M6OTX1BFTP3N~YJG2PiIw!3}JF1c_anKnmn<Z%8V8>V+<jjog)$3+IwY6=9Rheu@*^o<*J^H3}wz&{_?M6^zO;X;et7~*R'
    '{BqS)^>4%OdeF@|P*v|*hF$ZZE^6~wPeZ#<7ksCRB{$oN-iy5LDneYG2|s7{>CiGxwN!eFI9>j5-'
    '$=&ce;4w{!L^Nn`80b)8jO=|*2|J!ZaGsp1dE*4IQN@m5;2GV$tD?W;u11P!YD4mqz<RH!&Gw0mEu&Np!80ZGB~ivHz<@PcCO*qt'
    '}ppseFyqXSJSLpQRWEYB`IAmv~<vv7Bo^8u3>!8?5$}$h`16qn+uzOw4T4f+B|1IOS+<)cEr|k2lCL6wnL^j#^l-'
    'YUS*|M#=Vp7vKfxk6U_5MamZD3$@JwjZSc100H6+Jc2P@j%d4Orw>wJ+-`ngpJKGP}hBmlQuEU(6vIs-WRM{C?r4l!Nzg@*di%fj'
    'dbF@ljBWN~**sq0%(!?42Ca<MoW~gOZ#@n^yY<`k9*KKQbZWBCeWXay+IO9l$_s4I>{vq1PJ@rXXc+4574ytnM)#xPka4S7m)=$g'
    'SuiB|=E(OW`WSmsku~Y8R?1bfocQl8v*=MOoc4Rulp(za=qwZHC9oP!Oc86ht#cXdF3UdD7zvz==ub1qVGRue!Rc9Fl;QTrvpOg<'
    'KBK8*X(1^$~=GV!75Ftl}$V4_Gr&2`dFSbV7UXx$x2{`$OJp%tb-'
    '<6*%>Zhl!Lmj>;ZXNB6I}Tu!C$pe_t}@Z9;DlCi!c|Z|RT(!6?hUq|nssDgnEANEa(4!m7%q)ig=i=cY`Bb|Y^O^ugE1vP-'
    ')lEY9e#t_WzDF4&$2!g4aQPLdU|l3Ac}fQ9ph6qlFcG_J!Mv9o%%Pu_0$OCr^m?9BnhatI$mB=-7n8yStbX!-o5kt_joXa7`-'
    'p&FjycxEK&3?kQAWMS8MofO0inj+VMp%PF5IvTD4MXg<FN9%Q;~h*LJ)c(8e%c?2dG-++IiF+XQfP&JvmdV?{J^m@Rs&5~NPq-'
    '>$)ku7jP1E)I5L#7xnbeL-'
    'K{P0&>2bc9D|^IQ=1@Hjb_{6)CMTqpQEQ_H^JL1I5ym9KO$mr_<3Mf4XiNdfr@kFs!mCMTuhBL1NlOpPgHAt-vI#Oj#t59wKlQKI'
    '#u)GIV>zis2qm)XMZo)h4pmXwG|WPmC$Yu`nAW2W?xiOAb_hq$NA&l0uZ%o!j{%bsDn^|KqGc|q)dbc0NggAabhM9u9_pT?_qZ@='
    '69{V&-'
    'qDU?Hcs#XFL#1<!MUr?k$U)#*LS`$?D4x=hx$qNW+R25Uja*esKTLMBroSk6|Y#m~NbgFeC(0LpJ${ebUln)t0grKR+h*!WEUZ!>'
    '~0(G}G>JpL`LozZ!&&A~#7512*U$3LJCEsh}0O3X@zCn1on}}}t?e2-'
    'WO`$C&FmhR(H{`ZKQ6F=<D4xVsaeQK8&0NO~%h>WqUh$&{>;!R9EMbp$C#@Z}a0%*_F%!z~%=VE()5?uW`Kd>`Ole#9dFxF{AwMU'
    '2FedwAS<d;ztH}J6+1CTG`*(T=+7XHys^?>Dc^qR(pcV=?69itz6ERz>d=J#RkjF=iUCawwGzJ7+bN%ghM5o{~yMM=BHzv+ZIm%J'
    '~;#p33`G>=1hqA0%|3vl^fMu%I11R7Yr3}NRf8tD)dG0AOxXvT?m9z}Yjrowj4w7!u|CbVB-mmnhc>8Gj_R;k1|485d54t2Pt$=X'
    '`%8b>_<+k@-'
    'TrlQ9E`#DuXrSk|hqIY4>imu)?F<{1#nxIVq$kx(dHe>$9hH6xYjj9s;~T@KN&lT;Pn8P8MRR7wSv0LtuSCww!rbO=fNIut1;ZuI'
    'npQMk>WvF3M?s7Yrdu()7QjD7IK4tZ7Y?9G6!@`BP?Nskd1zD@B!!h`p+34BYP-'
    '#%?qfmyaq3F67V2A$Ok*^#8^Jhm3i^(CatsM~b>an)wgt}xAoOqA>>jya$FYKyQxx@}a;lkav^gum$PwdAHP`60u3r>h<UB{U6gk'
    'SzX|3d}cEvWJe%Ud<Tr007QuezDqUwAUZd~#c4^7QQSfF?$lK&QmEifK2g5*dE3a(Q}sv0!1z7Rzrw-'
    'JkJY&~jtiLnt{^C?2}(O-^&^}rF2PZle1{B%)Kr0{H#Py)ui3Bt-rawv0#Ge}7nVqnzJ8oQb!rnE^SA7?W2`v1zQC=f__)e^w;z@'
    'vbod%Hv>_zwH?q${Ri=F}_f7}EMA0x(}~7xl>}W*E*kCkDnyGr~T`P=>-'
    '#@X6xil3%3Yalk4Obvo~6fI<;bpCTtuiczw8hlh9BHCeQ6UMkl=x58+{YdSwplFp{~K{5;t=!$Zfo9_$}FbGx}K8+#5y(ZX>5Lxw'
    '`psEKkR9SB%%^eo-p-'
    '#M*oC50O|C1;nNep&}7h!yL+`{mJCh=&J<Tf@%bvjVyJyH))Z@^XR!o^F(YX<8rdiuT*g8slZ0idl0)IImWVi_r<rLJQ>nv7ob`k'
    '&@y3e0gf&0(f>nJbxMvbH;D3S!`rU0V#Arvwh?XAL&>fL01po|(e~Otc>*7<Q%!7LF*MzHkZ?Jd*4!O0bkmkfdEKkyEp5$&Q`UgX'
    'w2WsSFu+7DG^uLSLPr(I|4YjoRxHOJt<>mgmpvup1<Y*j?4O6lt&OKxUV2M9&@lx^<6-'
    'e(c~4cor(oy5r*#*yGL#s`*Eco6GM7i+zux&pj}(d(oj|;@7iSCvJc~TE|Ql)9UcelD6OC5$SEN=eZ<$@;xi(;`|FaoNuVTien73'
    '%Q51|yx*pN3@ARoCMM_R3{RwvI<OycyGV9BRw9NhTv%LQSy-yhUA}bX@<R&`trV^um!>XKrz)=aVY*382b0$pdc$DrnS{WUmI3PH'
    '5Xjn>l%Q^X)MZClvNjuS8MVxa%F9XB$36g})H5-'
    'Q*aOfk+deiHaxGhC>+n~XC>oZP!<8z^1`l+CrU=Tckbi`08713H5XKW`wqw5((vJ{W%{BE4cV%!_Z-kq36)aI^v!%%1mH8qzrPrc'
    '55S{GsXIe9%LCx7I%0f@FLQmR-o=gjEvqIZ;p>1ogPwBHr@5#tP@Ba&xYbvvnYL2b)0~NU)LN!Kv4-hjkVbL-'
    'd8&rb?32(X7$!#vK1+RHErev{S)8q_x3V&zpDkGYTWz<3+snyg0ioTVd1U;u7M2uiy=ONg~UD*Q_W8Pe^jBYNr9>87txF8w^J6Wl'
    'z(Mh4YC|w53<nJBj_mjn9S!^d{q`DH#6v+ba&qH4dsRbr_T%vR=v4JZo-1=Ci3Bd-AmMT7;lkQsiiC`^_h<8_N@M-'
    'arC$WO68>u^%rm0P*Hw#}ch^-'
    '8G+hrcL%iFkvF;3kH%R9>NCzapE>=A_>FU`ql?60aK$Q}!VfhSAE<nmJP94jg6wk?Kueqv4cpg~29i($IPT|lZ!#ah20=9-'
    'HEn}`8){xr_Qz?y;@z|>iR=}fyc)-GN`jWF>PpKgx@kC(U+CSH}m?!sM{$2psp(b8g{_A;F93=`VeI>X-'
    '5t+Te5z?#z%_baI%B>a17&*-%h@`S2zAlG@-BJHEFJ<={RYM*4qZ4W-Cnz-'
    '$KhHT2h6jMbkX$Qfg(C+9&X}mPPS0cOe$9M@}vS0ECz7)SE_V%VS5wehxxQNj)5&8P?fH68fOSxiKzS>QjWq&yMzJWXYdwRBXW>&'
    'JO>FsEj-c2bnp7wuSB9s0Y-'
    '`gt<0&YS;Q3#y&6}wp)6gxQp;|AREA;2j^Jdq+L(Hy+9;~IA3YG=oX?W{t?33W`=B!yFhDZ|_qlxeQQdpSEmW8GgfkG(JtU!Nyhe'
    'z@U7`C5;2t>u~zzheA1?>vj0W{)u+Pl<bv^C#h{J%&q9m6@y6?{@r7-xZk|(z89`i)s|tTv7+OB^)(GL!t_6Aa;hiWERKb#)e-'
    'HFeo3h)+4+a;x%zupslBOnbAyOh5kE|$bAOGJjq19q?`-'
    'uBZOTmW8Y+=3*nsrixI50QBe1d1Dm?*f;s9rPd&x%!Wmb&X+#{;OyG?acK1xB&${!nXM0W*b}>NOV!e9uB!)ROHkc_Hcs{G)wp~h'
    'D%D;>R=}4R~QsRx~k}~ZB^~BP*u>njHg*!uHtv2I6MUwg(Cm#*E$;H&3+l!a;HR6)F>%qy-_k$w5IFHhTP?@26y&-'
    '!<L7Tf}F_FU9?_5-'
    'C;34*WTB4BN9g^=xXV2B^vgvbFOiK*)ldR#Y^K+l!D$||b;i~e@vh>~olFI|4_&jcni&zDvi4r;Mhy)9*1<8(4eSy9#Na$Ld%#e~'
    'bz<xYe!(;DsbI(mM))JOqoKH1<#R@Hs&f+P}1iKu>?*|z?<~nFzW$)P2RCWb=1{)`UqjClZPi12>k-'
    't@r&(+uB;9R!gIziV^D)U~^`e~nRsm?cJBj+YKJ?rFd;q<rJQ{myxibI>Yg&h;V%!p^%b>qFv>kvOSj^|xkm|3%|m%zG&h4dJ*y+'
    'bVWxYAd82SfH{&Rba{KPocrL5PUaEFkc%9)ozo5z193F-2h6k65fY<w1>5C0~|#YXEwL|CE%Vf=*sJ2wND!p6)rp-'
    '*gV*Q<=^pgrUkp@lo%9@exxAF;og88p9Pc3r=Tg3bTa~!F|P?W)$|zD`xOZUQv~zMK}F~CMU0Aa&S_k1JOBcO#|#8G|nDVvDv#4q'
    'e^Z)^HK?WUf50|ua;2e8?0Ha{tzuwB{XgfQ&WUtF{;wIqoVp!r}cop2FrHe9WRkS`{)jMYB$^uxDx4)I{9r<S6j}hcF~h3?Ws996'
    'd6HUh!U1neOVkzIzk`QrI#KT_Py{jWy8Jf@^Hp2CcpXwC{z$PNaz&emxlFib(+m~60xYuSkwCCB1{Tnbe8W8k3Vki6Jq4%$<zH;7'
    'dvKY?NeuRA`OqIw+r(Ta@6IGWmeTa4Td~&Fsp%??zPsrOGx@OO(wCrc+NPDQ31(B!L<1k<P15Pf;%C@j_Cl{UgbE8%7vYKjDtBz#'
    'vjkAG2yB)kym5VRb$d!Bi-|sI~fnr%|vk=Q;Wjbbq`w%?qk8k<7MH&Lq?VN<LUcSUj515^&O{G6uEks6b!seIs~>eS|ro#pxa_-'
    '#p2yl;*qCQQRPa0pzoAbS#_+47Ou)dDZ~5J;%<;pOp49rBy3Bu*<aXdz5z2wQ+SN6HFv5ko5wH-'
    'TMa+%{M|79D|}cFTjqY5nZuNwBae-dD@`xKya=N$zXMBHlcykIxopdX@axq<tBnp^=&#kP$Iy-'
    'D4p|GDP3EE1++n?5#8Oi<2)sxKM8o=n7Dd?Z5~+tYf_&Y_VFozl!o~BCl+!FK0#uFz+vI6>8}cmuGkI3=Ok<80JKom3k5Md9^o>r'
    '?5?X<FkklGV5xWFqZ?Abi+)T^eJ8l+h`CbGRY_Xen6Z*%s^xMSvxK(!}+{7p|^gMLJ`nnp1adN5COoDbZpljo4vq&=xuP^&FeZnR'
    ')odJdM^!i^)y7OVQg^}7Fe!}d9q?-^(kp~uGD=ws@)PjU4l_)LEYAS(?wxcmE%Y+85gXxB~E`XS53^_RT$dhjXFY~Ubyt&3om-'
    '>Q*<q?)5r<5%^&7(-?a+#k{jwL6u?(CC(UgAMYoFA{Y^)IRGB@Xhu-%LEs1Ct*6QWnW_4P$4Yj({q?3Z^-'
    '32F_b8Hk4D@CpWG0ePo*JVH6~djXt*ijju^<Y{P@k+f|xL1SMIoGXiz<zsJ_SLzW+6tKXKw!az{HdL*0iPsR?rW{{?}was+{gc>T'
    'U;sQ{(cHF3fzEZmB+wfu^{zN|8I4;tCTV+33Tn%(?#iPb_LRj6vS~Rc>ebD@YcrhmbXJq;N#V>j-'
    'R%{?hjKPqCZgEB4g2ooEzJbxXoexpO1<HqLZ{%b_1=U1SMzd7}A+(pgKiWHvk!8(H=~p=s+UTsZIG&tg;6RfSQl`93PROFh0yAX;'
    '(-zWE2e_T#!HHX+O>YBKF-'
    'M6K89jR+)dA=q{pO#kc&|z6EI`RcaVonO&1HDCy=z8!*vqLCarx<5<d~k%aVZJ#nzu;`2p0zG&psouKOrcrE;uK%BEgA8mm;K9Ws'
    '6a-iX1(?>eY6JBG3dOn>3cc+d)TN(SNiZL_bQhM@Bd?z2+>xnyYl*Af*b%jZ5A1_~BQDRgMyqwvLR-'
    '?##w?bn%~wVwS!WbCpf7QL4tBAO%h*wld!@%b3bsMdoL=W`MBKU~WnRoZyX~fPNWQnnjEDO3#lu?eAiNr-z-'
    '^5vJNz4`b41P%@+4A{pz!RDHo>IA+XDnNCq~1`3vQNx`IH$w5_O&Aps9+A>|EXsHeMCekF>^a~FCQmDV8UE7K%mN-'
    '>Q<|#9)Aq|QYd0|FqE@?<AfDc3{>xm^XAy)cug3>9YqMmG03yo)!Qf1h^T*Xi*4LzMkxRO-'
    '&H$uy0L{$m%uTP6CO!+seozcl0*Q{lpCu-P!+xOdGpq`s2t91958mq`c^k^J-f!!{uHDG?TkN-'
    '~$^g#9?Jh5!U$D<6q%JelQiTcXjVc1zcy^lSpE6maA(*uTa%P#J)SB}IrCqG};*W<VYW`%UKq*;&Dz^5YL(S?i47qy&6_Z)fFD7J'
    '6dU)m_U$SGO`T|T^(fCsGNrC_p8oQ5@9<nX5Wby%`bf@xN*oV--FcOKTnrpz@|gHLmsV3cNXW`h%HgDkjEMw6JDyD7n?sj(z&jND'
    'jh33||}Ahgy5v4fwHB%6{XN}egvGVRfpS{)E(nBeM=lUYmZzjCWu(ZIaqRDnUktrR}G#VJ?zE`!oge9Pz!ZBDkLwCPCaMV}ur`lA'
    '6XZ|^)Q3a+rbse)-Rrxx-$&#Pt33_Xf(KXXIo1C!|L3Blxd=lF4s-ZVK{pK2K?#c=C}Jm|#@nq&kcO)J5&=o9(Z8UAB-mIb;<Kb-'
    '*3+xM>dtEcXntaqj|Yu#hlI(zy|%{y~;HK*1YeZ!o75N=zP9@H0x_J!WsLBDp1J0?|1VA=f+CL0aFS_ILD8_M()h%dvSH-_jvS|$'
    'XK2s`wSBnFEC{U)SoNhK_Rj{&-'
    'xH0=aSO)s@U59bT!rKQ6GYSU(T9UzVJTg!A=XOL5P{3c2Vn<ty$womUSBZ6%nb%Xfzb(#y+BVf_gMG91Q<pDdcFfO}>L%UNjxO!B'
    'XZk)eZIIi*3Hlagb3u#VW=-{_~+yus#1+Ag-HrBlN)Y*H^jNf;vL;f{KMR(NR!P*n|ojQHrnKOgerq}GxLgmSO@0;l8h_i^yl#3}'
    'FgIw7zAUCkI%qPjjy`3WYSG|jxfQr^vS^m6J1tWS?p}^n+L&7W?A(Qvq(@{5t%|d7UDKv3LmI6x$f0V}!W=B-'
    'U%afCcK4OPMcJH`kf2V{#MVe^u%!|tonylpzPv1kuGa9F-'
    '?vWh8&D~&atwXcWA_39nwt1&yv{9tCt`3Km;EQPa)Hn_McJDaaovZ;Zv9lLpPGH}wH7K7mwvU_qYJVIOul^mFvnZVCG{FfRqxrW('
    'P{;OGn<A|<m6%w4K43|&u_Hht5TYgev4HCm9$Q|0SVx$)3HIqm7$yyn<?RBu-^cF(av=;AEZ!>9o_C5iI4b=q0^0!W-'
    'bqe%{3HrnsT&n}Mx{Bjyrkuc6s(rqQCW!i3Bx?tBf^7Lst$cdYVGMqc0%(RBQwTMWDoDX6Q=8(1>gu$5FTaf!J|w$ICbdERf9m#6'
    '4i4efd<829}*Hv%dNwrhe<qM`H~NIT%5SW5s2P!KPGpfxY3~;G9PZWT;Ev`bk`S@3Og6W^>yttQ<hmUEp39pbP#*QN^k(J$SkeP<'
    '~FPx=eC2mfL8hzwV*)}rGHrj7O{M@rxuT<C8e}><jITDJNk6rWNqt$pFj%*0Z|m8PDa>d^~%!1@-'
    'n%fMX|1^hkYt&(QXdt6t&)<#l24r;ZeO6u{6;!U~bo37!*u5`4Q%zRmydPR<4XYH3bTkId(73j|jQKUJUBXFIS@lT!z>!Z0dzx^C'
    'Ap3MT5k|z>YPcZm@fdpn9Xq=5N!F>r?}ku+}h&G^mys#D*twX@H6%V#x>y==F}v89Hp&{ngGoYoV+RDzz<iS}oRDv4=f$WlCfc#Y'
    'x2r%c?;yr%z1L09`Y7eR>JCuDMiYx}9T$F)j8Nz9>7RSjexY1G@3KFGqA)8cAJ7*Ae4>e55+5V?;I@fz7-'
    '@dWgFj=mMnFl)gWz9Qyy;{f)!l;6SRT*E3u+W#Ubl^*r$QKo>rs8Qs}aJHIfpS=0U~R&<|UrRjL)xT+rQr4u~-'
    ';;lN}Rn<!=E_YRx&N`sc*;qfi2>6Xqyz8x8l>m8n4)kZQ*#s^EIlF;LgYcek45PFfZj%N(E)qg<VH*|`SfE8jhM08XRCJ~hxwz<x'
    'K@0b;b0eByjQ7zb9L0TO{EH#N+FsHCD<a+@^p-'
    'h294V)=&<OpJs;O2CkX<{lh!PGYt72AM5Uk!1t}qyttWdzC@ripeavZv!PT)Iy1x^3o<X-'
    '&eo}wd)haTW$h6ClnO)^7wP+k)TvU+4@t_`k0*)$mlN10)J5B$ZW_=yR`(^^1^u6WY+g(x08qr?F84uP?!G1;2GMVi1=8(=ub$Q3'
    '@JK08e|P0>#<3KRAK>rn?-atHL)%^>W=21D%7w8J!D%)}%orH$?+MR&5)gMLUU#Sm1QIL(_F&un7i6m4T-'
    'qGu!b%2u?SObbE?>heKX-}s`NZXj?4T-X|mYb+NRV;xgTMtzJwNrt2DxBMkY3RPvNPV({-'
    '*VgrywjJ=)Q*!W+ISMKXh5(XULa}VLLQZg+@nSpsPPzJ~8@<L|09nd1ZWU3hg;6mHV`)ShDtl>!X(vL;2`=vyjg8}?&Qw2uGy>_M'
    'T!vpZ0n(w1nXcv-'
    ')S?Z=Wgsf_G2)It#v2{Q6X3_Ws$CLCDKV7@>nyowENxF_C0+x<9@nNsMz+AOU`kk)a>k5tvaq_{kv%K)t&B`g=gP25)FbO_TS83l'
    'wUW9F!OP}o+gk=&UXotfZDaW}y#^~^xij2mP|~iQk>oa$#+t55`E<+ot~+)V2)<7@=@gJT=_B<Nxha~8f&7>T_t2TU-AWC=Y?UIf'
    'HCo2Ss^E%QmR8YuU7aOGcM4EvT6CA*!-NA!gdE|ZXVpVRfw8U-2X<1@o;=<we`yA|%tYNo<psC$VGfxY+^&{{ZP3-'
    '$QNxfslu@ujcY{{=w@PJ&U#q$Zs((+A{x+)UYPPR5GnYHNOm32|(pOu4a**<it;9}glTVErs0nxut`K;RzXJ8olc|)lA*@AVi-ty'
    '{s<<D~lqg$4?7NxO22jTsqv%;RP=!aLAQ`L)G;zI1>nVG4xm~GT^y7HpyB&FEpABoZ3AGnmo}Db9M;(LA4~TclPLm^zYtQvjl~j&'
    '(@+*=hu4TeX25YPlhgvJ#2NTY&9&W)nqJ={RW7`p#c=ujPq$b(tA~FO6<T64ALpYj|n9i67N#=hU24Sk@I_l{$1dEzBO+zv0>?E$'
    'I_IVyz>UADDqMb*U`Z|RiF;5{&s@m2|%eNF*DH=3+kZC*;Ek;C78wx8JuZ~8`bZOVBo=epWrXFj`Mf$3Y7h$DR{IIP_D@9|LDMyQ'
    'z&&uM7{DfVB?;*PmYQ?G?r2*FaOx%8S+Om3N{12)ubN?l-GcyFfd6ii;*s>JZ3@hF#$M%Z-'
    '&IMuWX|q9nsn3V=F3V6WuFna*ygDniN<HU=fYa~HP@(^GZuB`f1kwK9W{18V@s-'
    'XG%`Jp27U@2tzGCR_%d0X{VqQ39J}sn`rTSpYeRdOcC-'
    '&Ag?Ve(H72j4W^E)kXBdFy}46RQE1NdPlhr^HNA9dxxTUElBo#(#&<`)iad)zrM2GM^9m>1fnxi&MbFT>3Z<GO&H9X9wQogWtXf6'
    'kD8X9y$R-`yNBPkxSOmN@dGxI?d6x6>*W>lM2SNIds@r(Ywl5-'
    'AT#uKL2U3|Qsm#EX(n+Zf>dfo1M>ICrsX#y}e3g7(bFM2Bdf$3SD3#d@HT*>9Kz@YE_4b8%Kqk9e<V1{lFg1;aP6WWVga9tu`k)v'
    'mFmmE~rE=9|S)fr-t2(Qpb6Q9nh|jB$YL+OF$ski&>(r{kCgF-'
    '&J~6DW;)lJ=0R1M4F2k&K4wmfBJ}jj}O|9P+x=+San4(9jjQA6iiBmzay_+|{M!%S+lrb$)UA+yfUE=8b5VSz(a}4t=XRdRh(-'
    '<I2$MUZ>p0R0CNu{6q&Ba|}Mk-}I8xU(;ACn`UO5^bPq8%k&Axpv>W<__YM%9W)NDD4)jdVi#$%3$W-'
    '(we+_vYAFRQC!crSuQCU3W0R~<Jn}HNM;ysk9nEhxnr&7&HV(Z@(2+V!SX?tAsu6Ig&seb-uO1K81`nqD&j`H3M&M-'
    '{JInnQK{={BlbKY^^%0&|A2&x(UUP`Z$jCqq96J7QuLh4Y?ys`)DHKJ8MmEec_6@qs@feHp+-'
    'Mb{e6wqv^CXEM5$PvD*4Ylx3=_kJEN$6E3>0Kh*o-'
    'UWk#o!QG?Gq<ETjE0HkU+A8Q!r(Fl2&aB3(Pa+z8fq_(cp@gx4xDzD$WM#5KGyl2AhCH!su;^misSFh-pujVSD_H|RKRFm9(FZm5'
    'W|5E^2H_!U`t<7tj37G(XVu}&%#7-X>9sx|zkuAObnDF+y@1T;Vyt&4G!QdO}YcJSD}>BXQaLf8I<g&oZJ`l$Am-QL3dUi9O#x6w'
    '9ljL;%mUVJ{Rbz&X_ir!yh{BtfzqF~jfo}RxvcXb&wQF*0lW$EgI+BM24@nY+Ile=VFx`DCLB5$^`t7V-Qrj0&t-'
    'aTVQwi@tS7$Bpw%Z;$ptONC8pUMT@HIIRyZ&jd<Jaj4I)E;z?h`Q{mk*LfuuXiFk6Q|iA%@!2EquR(9%QbW)>AwXv99B6a8kPdf;'
    'Ib4CEt8=$I(b?^(E3W)Y%XkSxmjfBVR`e-'
    '!7$%mc(l5(Ja_KOf^iMb+S1|H5v#2Ztph4@osn5DeHa@9@!s??c^}4TY5M5$$U^VM)c1)8C#d_ifCdKHYWPWmM!*f}Us_g!x8Z^1'
    '1l2pM8$kl#%2cx=z??9dI~(noKs}t02|C8Eb`o|2Z3{br6_YU3;9)@;o`}XmKtwQPML8`awg#^NaFP4RqP)fJMY0NZ({)EUL8=y('
    '_2{SKqGiUYJ)-<xwnLz$5JkY$9`NRs-'
    '{<n+(HN^efGuA+H@BcXUtBo9Qk}c}(8|)~i&?Nm7}nklOQ9`4xOjfWyt}D*3p@Yo4SQ+v!h>$$+OTmEzhMi1^XsG5SB}J7CyK-'
    '9N@x;sgaH*l9<?K@QBfQm?drgJ)>@ZPn@XF)r*vJiWQ4XJvTjUelz3E>c+|Y>W3P-Z-'
    '!_$VWSC4P_S)pG8DDab_yEREOV$gHYiHXEy~i10R3SmJ)PK!K)tZkM_oj}PuF8J5rd7y+)uSP{oDq-'
    '5r!tGkSjP@FKc5!EV;z&(&4%<>j`)SjcsY=?Ud@tmol^Jb9V^*k7%N(zGFq4Yx8DujKJPk5z=%9(XDqVg;pXKdvCm5s$x`!LOCph'
    'oi=L>oIXkM^ei{FrFf>riXQ9XN6|Gy+McMA-iKr?SYNApWgLNE!S{~r^t)6x%GvSM)j?P_q%-'
    '0Ks<dIw+azd$L#`p70kNU#7s6bP~vq&mWWfz!7=i}@KL?jj&y%HqoX%?vW%xU`P>{%YXcP|?XZ3J)Gmz4<mnj>+_vqFnX2eCjQxl'
    'Sxa(uu`*iLx>%hQs--FuMb9AY)UYR8q^4>697AjRMd4Ag+OKrlvxRGp>yabZLZcC23lrSW>@TL^b$=-'
    '}0GJQgB`%HH|LENvC^l>o37suA{J#_9(+yxc82V+)AMba$_L>uJ_?C+(*wdJb-'
    '&jNq4{Vb2tsvdww37dP_5QAdz6>Bc+40TiFKoA-VjhIX#<=UAbpyHq}Eou&1J64KM3A`jQ2Qq}EYWY(Ng_O=Py~P%xJ1yrvb+K^o'
    'wSaA)2#%k;|;QK|2W772S)eN6B5<Aq%AQuxx;YG|yy(a}N3Xk8_6vA-'
    'm6Ps9dq{16=dRdIFiIV|TZ<GncU{~pSK(<bMt$zIPTY0ol42Uul~dUfs5={+MdzPf9xh87zc$-'
    'S~}b%TZCNF(PAo~BTDU9goz1vn4&axC?80d%eJ`KizHFR$rQ)-o6;|E9h;-'
    'nek(D$4{(VNP+C0ab$_CLSe?YaI77#*8+KJC{`!g}qXVo4((!;;=W>{qB?c*7!!ygrQ?w)`f{PMJs4jU#4f)<$sMvnfc*loRjlD#'
    ';@dOK3idQot-Yw@HcGJdrV||2{0Na;7(xR@O%o2w2;ZQc(W}1j|gavnb+9nE+bKy5OEsEVSOhITCtLWjTfAf0BIZ^c*70|{bsm@h'
    '|~jby%o~HKOo>5w4O(;j7}@|*66Ns4SEzqzsvIYQixtKZaS@&ZQ8I*iS?$Rd0@jL`d4ukp5;mTbhBDtju*E5S|@=CS2y!7JciBCg'
    '^xA}gP4SoPj#&lZ<|K@uhJ9OYQ%Q(F{ynq&qq~KN<X|YSx|7F6#<AJEishvkgs6l;?=&}2_l~!H907nu{)umGc6HyPXFH9{=Md)%'
    '<}1;bj4!6rz(o$zGyInWxVA@NT>>3Qz^5p(K(1~MIFOuPeGA@hWW#zk(keyOds7otkzzev%?`=qsKa!9dT7ikl~iW{)Sx!^vLrA='
    '*tJd8FwcQ9Hd)uhND2Qm@r(pi4!-KAVxmUz5fkGUY#B'
)


def normalize_bytes(data):
    text = data.decode("utf-8-sig")
    return text.replace("\r\n", "\n").replace("\r", "\n")


def text_hash(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = tempfile.NamedTemporaryFile(
        mode="wb", prefix=path.name + ".", suffix=".v4.tmp",
        dir=str(path.parent), delete=False)
    temporary = Path(handle.name)
    try:
        with handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(str(temporary), str(path))
    except BaseException:
        try:
            temporary.unlink()
        except OSError:
            pass
        raise


def unique_backup_path(root):
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    candidate = root / (".terrain_render_demo_v4_backup_" + stamp)
    suffix = 1
    while candidate.exists():
        candidate = root / (".terrain_render_demo_v4_backup_" + stamp + "_" + str(suffix))
        suffix += 1
    return candidate


def main():
    root = Path(__file__).resolve().parent
    print(VERSION)
    if not (root / "CMakeLists.txt").is_file() or not (root / "Module").is_dir():
        print("error: place this script in the OpenglLearn repository root", file=sys.stderr)
        return 2

    try:
        compressed = base64.b85decode("".join(PAYLOAD).encode("ascii"))
        raw = zlib.decompress(compressed)
        if hashlib.sha256(raw).hexdigest() != PAYLOAD_SHA256:
            raise ValueError("embedded payload checksum mismatch")
        targets = json.loads(raw.decode("utf-8"))
    except Exception as error:
        print("error: invalid embedded payload: " + str(error), file=sys.stderr)
        return 2

    desired_hashes = {name: text_hash(text) for name, text in targets.items()}
    originals = {}
    pending = []
    mismatches = []

    # Complete the whole preflight before creating a backup or touching a file.
    for name, desired_text in targets.items():
        path = root / name
        if path.is_file():
            data = path.read_bytes()
            try:
                current_hash = text_hash(normalize_bytes(data))
            except UnicodeError:
                mismatches.append(name + " (not UTF-8 text)")
                continue
            if current_hash == desired_hashes[name]:
                continue
            if current_hash not in EXPECTED_BASE_HASHES.get(name, []):
                mismatches.append(name + " (unexpected content)")
                continue
            originals[name] = data
            pending.append(name)
        elif path.exists():
            mismatches.append(name + " (not a regular file)")
        elif EXPECTED_BASE_HASHES.get(name):
            mismatches.append(name + " (required V3 file is missing)")
        else:
            originals[name] = None
            pending.append(name)

    if mismatches:
        print("error: repository does not match the supported V3 base; no files were changed",
              file=sys.stderr)
        for mismatch in mismatches:
            print("  - " + mismatch, file=sys.stderr)
        print("Apply V1, V2 and V3 first, or restore locally edited files and retry.",
              file=sys.stderr)
        return 1

    if not pending:
        print("visual terrain V4 is already applied")
        return 0

    backup = unique_backup_path(root)
    existing = [name for name in pending if originals[name] is not None]
    try:
        if existing:
            for name in existing:
                destination = backup / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(str(root / name), str(destination))
    except Exception as error:
        try:
            if backup.exists():
                shutil.rmtree(str(backup))
        except OSError:
            pass
        print("error: could not create backup; no source files were changed: " + str(error),
              file=sys.stderr)
        return 1

    written = []
    try:
        for name in pending:
            atomic_write(root / name, targets[name].encode("utf-8"))
            written.append(name)
    except BaseException as error:
        rollback_errors = []
        for name in reversed(written):
            try:
                original = originals[name]
                if original is None:
                    (root / name).unlink()
                else:
                    atomic_write(root / name, original)
            except BaseException as rollback_error:
                rollback_errors.append(name + ": " + str(rollback_error))
        print("error: installation failed and rollback was attempted: " + str(error),
              file=sys.stderr)
        for rollback_error in rollback_errors:
            print("  rollback error: " + rollback_error, file=sys.stderr)
        return 1

    print("installed " + str(len(pending)) + " file(s)")
    if existing:
        print("backup: " + str(backup.relative_to(root)))
    print("Build and run on Windows:")
    print("  cmake -S . -B build -A x64")
    print("  cmake --build build --config Debug --target terrain_render_demo")
    print(r"  .\bin\Debug\terrain_render_demo.exe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

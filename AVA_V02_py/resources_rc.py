# Resource object code (Python 3)
# Created by: object code
# Created by: The Resource Compiler for Qt version 6.10.2
# WARNING! All changes made in this file will be lost!

from PySide6 import QtCore

qt_resource_data = b"\
\x00\x00\x08\xa9\
/\
* Modern AVA Sty\
les \xe2\x80\x93 2026 cle\
an flat design *\
/\x0a\x0a* {\x0a    font-\
family: system-u\
i, -apple-system\
, \x22Segoe UI\x22, Ro\
boto, sans-serif\
;\x0a}\x0a\x0a/* Base */\x0a\
QWidget[role=\x22ma\
in-window\x22],\x0aQMa\
inWindow > QWidg\
et {\x0a    backgro\
und-color: #f8fa\
fc;\x0a}\x0a\x0a/* Title \
*/\x0aQLabel[role=\x22\
label-title\x22] {\x0a\
    font-size: 2\
8px;\x0a    font-we\
ight: 700;\x0a    c\
olor: #0f172a;\x0a \
   padding: 16px\
 0 8px;\x0a}\x0a\x0a/* De\
scription */\x0aQLa\
bel[role=\x22label-\
description\x22] {\x0a\
    font-size: 1\
5px;\x0a    color: \
#64748b;\x0a    pad\
ding-bottom: 16p\
x;\x0a}\x0a\x0a/* Primary\
 Button \xe2\x80\x93 Impo\
rt */\x0aQPushButto\
n[role=\x22import-v\
ideo\x22] {\x0a    bac\
kground: #0ea5e9\
;\x0a    color: whi\
te;\x0a    border: \
none;\x0a    paddin\
g: 12px 24px;\x0a  \
  border-radius:\
 8px;\x0a    font-w\
eight: 600;\x0a    \
min-width: 180px\
;\x0a}\x0aQPushButton[\
role=\x22import-vid\
eo\x22]:hover { bac\
kground: #0284c8\
; }\x0aQPushButton[\
role=\x22import-vid\
eo\x22]:pressed { b\
ackground: #0369\
a1; }\x0a\x0a/* Select\
ed state */\x0aQPus\
hButton[role=\x22im\
port-video-selec\
ted\x22] {\x0a    back\
ground: #0f766e;\
\x0a    color: whit\
e;\x0a    border: n\
one;\x0a    padding\
: 12px 24px;\x0a   \
 border-radius: \
8px;\x0a    font-we\
ight: 600;\x0a}\x0a\x0a/*\
 Metadata inputs\
 */\x0aQLineEdit[ro\
le=\x22metadata-inp\
ut\x22], QDateEdit[\
role=\x22metadata-i\
nput\x22] {\x0a    bac\
kground: white;\x0a\
    border: 1px \
solid #cbd5e1;\x0a \
   border-radius\
: 6px;\x0a    paddi\
ng: 10px 12px;\x0a \
   font-size: 14\
px;\x0a    min-heig\
ht: 40px;\x0a}\x0aQLin\
eEdit[role=\x22meta\
data-input\x22]:foc\
us,\x0aQDateEdit[ro\
le=\x22metadata-inp\
ut\x22]:focus {\x0a   \
 border-color: #\
0ea5e9;\x0a    back\
ground: #f0f9ff;\
\x0a}\x0a\x0a/* Confirm b\
utton */\x0aQPushBu\
tton[role=\x22confi\
rm\x22] {\x0a    backg\
round: #10b981;\x0a\
    color: white\
;\x0a    font-weigh\
t: 700;\x0a    padd\
ing: 12px 32px;\x0a\
    border-radiu\
s: 8px;\x0a}\x0aQPushB\
utton[role=\x22conf\
irm\x22]:hover { ba\
ckground: #05966\
9; }\x0aQPushButton\
[role=\x22confirm\x22]\
:pressed { backg\
round: #047857; \
}\x0aQPushButton[ro\
le=\x22confirm\x22]:di\
sabled {\x0a    bac\
kground: #94a3b8\
;\x0a    color: #e2\
e8f0;\x0a}\x0a\x0a/* Game\
 Metadata group \
box */\x0aQGroupBox\
 {\x0a    font-size\
: 15px;\x0a    font\
-weight: 600;\x0a  \
  color: #0f172a\
;\x0a    border: 1p\
x solid #e2e8f0;\
\x0a    border-radi\
us: 8px;\x0a    mar\
gin-top: 16px;\x0a \
   padding: 20px\
 16px 16px;\x0a    \
background: whit\
e;\x0a}\x0aQGroupBox::\
title {\x0a    subc\
ontrol-origin: m\
argin;\x0a    subco\
ntrol-position: \
top left;\x0a    le\
ft: 12px;\x0a    pa\
dding: 4px 12px;\
\x0a    background:\
 #f8fafc;\x0a    bo\
rder-radius: 6px\
;\x0a    color: #33\
4155;\x0a}\x0a\
"

qt_resource_name = b"\
\x00\x08\
\x08\x01V\xc3\
\x00m\
\x00a\x00i\x00n\x00.\x00q\x00s\x00s\
"

qt_resource_struct = b"\
\x00\x00\x00\x00\x00\x02\x00\x00\x00\x01\x00\x00\x00\x01\
\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\
\x00\x00\x01\x9cc\x1aT\xd2\
"

def qInitResources():
    QtCore.qRegisterResourceData(0x03, qt_resource_struct, qt_resource_name, qt_resource_data)

def qCleanupResources():
    QtCore.qUnregisterResourceData(0x03, qt_resource_struct, qt_resource_name, qt_resource_data)

qInitResources()

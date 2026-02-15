# Resource object code (Python 3)
# Created by: object code
# Created by: The Resource Compiler for Qt version 6.10.2
# WARNING! All changes made in this file will be lost!

from PySide6 import QtCore

qt_resource_data = b"\
\x00\x00\x08\xa0\
/\
* Modern AVA Sty\
les \xe2\x80\x93 2026 cle\
an flat design *\
/\x0a* {\x0a    font-f\
amily: \x22Segoe UI\
\x22, system-ui, -a\
pple-system, san\
s-serif;\x0a}\x0a\x0a/* B\
ase */\x0aQWidget[r\
ole=\x22main-window\
\x22],\x0aQMainWindow \
> QWidget {\x0a    \
background-color\
: #f8fafc;\x0a}\x0a\x0a/*\
 Title */\x0aQLabel\
[role=\x22label-tit\
le\x22] {\x0a    font-\
size: 28px;\x0a    \
font-weight: 700\
;\x0a    color: #0f\
172a;\x0a    paddin\
g: 16px 0 8px;\x0a}\
\x0a\x0a/* Description\
 */\x0aQLabel[role=\
\x22label-descripti\
on\x22] {\x0a    font-\
size: 15px;\x0a    \
color: #64748b;\x0a\
    padding-bott\
om: 16px;\x0a}\x0a\x0a/* \
Primary Button \xe2\
\x80\x93 Import */\x0aQPu\
shButton[role=\x22i\
mport-video\x22] {\x0a\
    background: \
#0ea5e9;\x0a    col\
or: white;\x0a    b\
order: none;\x0a   \
 padding: 12px 2\
4px;\x0a    border-\
radius: 8px;\x0a   \
 font-weight: 60\
0;\x0a    min-width\
: 180px;\x0a}\x0aQPush\
Button[role=\x22imp\
ort-video\x22]:hove\
r { background: \
#0284c8; }\x0aQPush\
Button[role=\x22imp\
ort-video\x22]:pres\
sed { background\
: #0369a1; }\x0a\x0a/*\
 Selected state \
*/\x0aQPushButton[r\
ole=\x22import-vide\
o-selected\x22] {\x0a \
   background: #\
0f766e;\x0a    colo\
r: white;\x0a    bo\
rder: none;\x0a    \
padding: 12px 24\
px;\x0a    border-r\
adius: 8px;\x0a    \
font-weight: 600\
;\x0a}\x0a\x0a/* Metadata\
 inputs */\x0aQLine\
Edit[role=\x22metad\
ata-input\x22], QDa\
teEdit[role=\x22met\
adata-input\x22] {\x0a\
    background: \
white;\x0a    borde\
r: 1px solid #cb\
d5e1;\x0a    border\
-radius: 6px;\x0a  \
  padding: 10px \
12px;\x0a    font-s\
ize: 14px;\x0a    m\
in-height: 40px;\
\x0a}\x0aQLineEdit[rol\
e=\x22metadata-inpu\
t\x22]:focus,\x0aQDate\
Edit[role=\x22metad\
ata-input\x22]:focu\
s {\x0a    border-c\
olor: #0ea5e9;\x0a \
   background: #\
f0f9ff;\x0a}\x0a\x0a/* Co\
nfirm button */\x0a\
QPushButton[role\
=\x22confirm\x22] {\x0a  \
  background: #1\
0b981;\x0a    color\
: white;\x0a    fon\
t-weight: 700;\x0a \
   padding: 12px\
 32px;\x0a    borde\
r-radius: 8px;\x0a}\
\x0aQPushButton[rol\
e=\x22confirm\x22]:hov\
er { background:\
 #059669; }\x0aQPus\
hButton[role=\x22co\
nfirm\x22]:pressed \
{ background: #0\
47857; }\x0aQPushBu\
tton[role=\x22confi\
rm\x22]:disabled {\x0a\
    background: \
#94a3b8;\x0a    col\
or: #e2e8f0;\x0a}\x0a\x0a\
/* Game Metadata\
 group box */\x0aQG\
roupBox {\x0a    fo\
nt-size: 15px;\x0a \
   font-weight: \
600;\x0a    color: \
#0f172a;\x0a    bor\
der: 1px solid #\
e2e8f0;\x0a    bord\
er-radius: 8px;\x0a\
    margin-top: \
16px;\x0a    paddin\
g: 20px 16px 16p\
x;\x0a    backgroun\
d: white;\x0a}\x0aQGro\
upBox::title {\x0a \
   subcontrol-or\
igin: margin;\x0a  \
  subcontrol-pos\
ition: top left;\
\x0a    left: 12px;\
\x0a    padding: 4p\
x 12px;\x0a    back\
ground: #f8fafc;\
\x0a    border-radi\
us: 6px;\x0a    col\
or: #334155;\x0a}\x0a\
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
\x00\x00\x01\x9cb\xf8\xa0C\
"

def qInitResources():
    QtCore.qRegisterResourceData(0x03, qt_resource_struct, qt_resource_name, qt_resource_data)

def qCleanupResources():
    QtCore.qUnregisterResourceData(0x03, qt_resource_struct, qt_resource_name, qt_resource_data)

qInitResources()

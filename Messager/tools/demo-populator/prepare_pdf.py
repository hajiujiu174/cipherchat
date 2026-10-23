"""Prepare the real demonstration attachment before the native sender starts."""
from pathlib import Path
import os
import sys
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import Paragraph, SimpleDocTemplate, Spacer

destination = Path(sys.argv[1])
if destination.exists():
    raise SystemExit("Existing attachment kept")
pdfmetrics.registerFont(TTFont("YaHei", "C:/Windows/Fonts/msyh.ttc", subfontIndex=0))
pdfmetrics.registerFont(TTFont("YaHeiBold", "C:/Windows/Fonts/msyhbd.ttc", subfontIndex=0))
title = ParagraphStyle("Title", fontName="YaHeiBold", fontSize=25, leading=35,
                       textColor=colors.HexColor("#6554d9"), spaceAfter=12)
sub = ParagraphStyle("Sub", fontName="YaHei", fontSize=10, leading=16,
                     textColor=colors.HexColor("#788196"), spaceAfter=20, wordWrap="CJK")
heading = ParagraphStyle("Heading", fontName="YaHeiBold", fontSize=13, leading=20,
                         textColor=colors.HexColor("#242938"), spaceBefore=12, spaceAfter=6)
body = ParagraphStyle("Body", fontName="YaHei", fontSize=10.5, leading=18,
                      textColor=colors.HexColor("#424a60"), wordWrap="CJK", alignment=TA_LEFT)
story = [Paragraph("Messager 交互说明", title),
         Paragraph("CipherChat · 桌面客户端演示资料 / V0.3.6", sub),
         Paragraph("本文件为真实可打开的演示附件。讨论中的人物、任务及出行安排均为虚构示例，账号与消息通过 CipherChat 正常注册和发送。", body)]
sections = [
    ("01  聊天与搜索", "Enter 发送消息，Shift + Enter 换行。Ctrl + F 搜索当前会话；长消息保留段落，方便查看气泡宽度与阅读位置。"),
    ("02  联系人与临时会话", "发送好友申请，对方同意后成为好友。临时会话用于文字沟通，成为好友后可以发送文件。"),
    ("03  创建群聊", "填写群聊名称，展开成员下拉菜单并勾选好友。支持搜索、连续多选和取消勾选；创建者自动加入，最多再选择 63 位好友。"),
    ("04  文件交互", "在好友私聊中发送文件，文件传输页显示实际进度与完成状态。可打开文件、打开所在文件夹，或通过“对话位置”返回对应消息。当前群聊附件入口尚未开放。"),
    ("05  主题与窗口", "支持跟随系统、浅色、深色和字号设置。拖动会话分栏调整宽度，检查小窗口和多行输入时的布局。"),
    ("06  本次演示资料", "产品设计讨论：布局与视觉。Messager 开发组：功能与协作。周末出走计划：日常聊天。附件包括本 PDF、图标 SVG、静态 HTML 原型、品牌素材 ZIP 和评审清单。"),
]
for name, text in sections:
    story.extend([Paragraph(name, heading), Paragraph(text, body)])
story.extend([Spacer(1, 15), Paragraph("“对端签收”表示通信层确认，不表示对方已读。文件尺寸与校验结果以实际传输为准。", sub)])
temporary = destination.with_name("interaction-preparing.pdf")

def footer(canvas, document):
    canvas.setStrokeColor(colors.HexColor("#e7eaf2"))
    canvas.line(48, 42, A4[0] - 48, 42)
    canvas.setFont("YaHei", 8)
    canvas.setFillColor(colors.HexColor("#788196"))
    canvas.drawString(48, 27, "Messager / 演示资料")
    canvas.drawRightString(A4[0] - 48, 27, str(document.page))

SimpleDocTemplate(str(temporary), pagesize=A4, leftMargin=48, rightMargin=48,
                  topMargin=42, bottomMargin=58, title="Messager 交互说明 · 演示资料",
                  author="Messager 演示资料").build(story, onFirstPage=footer, onLaterPages=footer)
if destination.exists():
    raise SystemExit("Sender already created its attachment; prepared alternative left separate")
os.replace(temporary, destination)
print(destination)

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate Hi18nBuiltinPages.cpp from all I18N/W18N keys in project sources."""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from en_glossary import EN_GLOSSARY

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {"imgui", "x64", "tools", "i18n_samples", "doc", ".vs"}
SKIP_FILES = {"Hi18nBuiltinPages.cpp", "Hi18nBuiltin.cpp", "Hi18n.cpp"}

def safe_write_text(path: Path, text: str) -> None:
    payload = text.encode("utf-8")
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "wb") as f:
        for i in range(0, len(payload), 8192):
            f.write(payload[i : i + 8192])
    if path.exists():
        path.unlink(missing_ok=True)
    tmp.replace(path)



I18N_PATTERNS = [
    re.compile(r'I18N\s*\(\s*u8"((?:[^"\\]|\\.)*)"'),
    re.compile(r'I18N\s*\(\s*"((?:[^"\\]|\\.)*)"'),
    re.compile(r'W18N\s*\(\s*u8"((?:[^"\\]|\\.)*)"'),
    re.compile(r'W18N\s*\(\s*"((?:[^"\\]|\\.)*)"'),
]
STR_RE = re.compile(r'u8"((?:[^"\\]|\\.)*)"|"((?:[^"\\]|\\.)*)"')

TC2SC = str.maketrans(
    "體國學網檔臺畫設啟務儲磁碟優歷關於後時個這為與據復執權許護診斷傳遞暫無現動監測釋內處進請選語頁開閉顯結說備註冊單擊點瀏覽軟硬線連離狀態詳細資訊報錯誤警風險確認還原備份載入匯導覽搜尋篩過濾項目類別標籤欄列表圖區塊組件視窗層級階段步驟流程計畫進度條百分比數量總計累積平均最大最小高低快慢強弱開關閉啟用禁用允許拒絕失敗成功完成待機運行中已尚未否是僅只仍尚還再又更較超過低於等於類似相同異常見聞知道解釋幫助提示警告注意備註範例測試範本模板預設預覽查看閱讀寫入編輯修改更新刷新重載重試重設復原還原備份還原點管理員權限提權升級管理模式進階標準快速深度自訂自動手動操作工作日誌紀錄記錄報表統計數據資料夾目錄路徑位置名稱大小類型版本號碼序號識別碼帳號用戶登入登出退出關閉離開結束繼續暫停中斷取消確定同意拒絕是否好的知道了收到謝謝歡迎再見拜拜您你們它他她其他這那些哪裡怎麼為什麼多少幾個種類樣式種類方式方法法則規則政策條款協議合約許可證書憑證授權隱私權利義務責任聲明免責版權所有權保留商標註冊專利著作權法律法規條例細則辦法通知公告消息郵件短信電話傳真視訊會議通話聊天討論評論回覆轉寄分享連結網址鏈接地址位置坐標經緯度方位角度距離速度加速度力量重量質量體積面積長度寬度高度深度厚度直徑半徑周長密度濃度純度強度硬度軟度韌性彈性塑性粘性活性穩定性可靠性安全性保密性完整性一致性準確性精度誤差偏差極差",
    "体国学网档台画设启务储磁盘优历关于后时个这为与据复执权许护诊断传递暂无现动监测释内处进请选语页开闭显结说备注册单击点浏览软硬线连离状态详细资讯报错误警风险确认还原备份载入汇导览搜寻筛过滤项目类别标签栏列表图区块组件视窗层级阶段步骤流程计画进度条百分比数量总计累积平均最大最小高低快慢强弱开关闭启用禁用允许拒绝失败成功完成待机运行中已尚未否是仅只仍尚还再又更较超过低于等于类似相同异常见闻知道解释帮助提示警告注意备注范例测试范本模板预设预览查看阅读写入编辑修改更新刷新重载重试重设复原还原备份还原点管理员权限提权升级管理模式进阶标准快速深度自订自动手动操作工作日志纪录记录报表统计数据资料夹目录路径位置名称大小类型版本号码序号识别码帐号用户登入登出退出关闭离开结束继续暂停中断取消确定同意拒绝是否好的知道了收到谢谢欢迎再见拜拜您你们它他她其他这那些哪里怎么为什么多少几个种类样式种类方式方法法则规则政策条款协议合约许可证书凭证授权隐私权利义务责任声明免责版权所有权保留商标注册专利著作权法律法规条例细则办法通知公告消息邮件短信电话传真视讯会议通话聊天讨论评论回复转寄分享连结网址链接地址位置坐标经纬度方位角度距离速度加速度力量重量质量体积面积长度宽度高度深度厚度直径半径周长密度浓度纯度强度硬度软度韧性弹性塑性粘性活性稳定性可靠性安全性保密性完整性一致性准确性精度误差偏差极差",
)

EN_PHRASES = {
    "優化掃描": "Optimization Scan",
    "套用": "Apply",
    "工作管理員": "Task Manager",
    "系統清理": "System Cleaner",
    "清理歷史": "Clean History",
    "硬碟健康": "Disk Health",
    "磁碟分析": "Disk Analysis",
    "執行最佳化": "Run Optimization",
    "進行中": "In progress",
    "已最佳化": "Optimized",
    "狀況良好": "Healthy",
    "需要最佳化": "Needs optimization",
    "取消": "Cancel",
    "確定": "OK",
    "刪除": "Delete",
    "關閉": "Closed",
    "重新掃描": "Rescan",
    "開始掃描": "Start Scan",
    "停止": "Stop",
    "暫停": "Pause",
    "繼續": "Continue",
    "設定": "Settings",
    "關於我們": "About",
    "版本": "Version",
    "授權": "License",
    "準備掃描…": "Ready to scan…",
    "掃描完成": "Scan complete",
    "略過：網路磁碟": "Skipped: network drive",
    "掃描失敗": "Scan failed",
    "掃描中…": "Scanning…",
    "系統優化": "System Optimize",
    "系統清理工具": "System Cleaner",
    "文件地圖": "File Map",
    "硬盤健康度檢測": "Disk Health",
    "語言": "Language",
    "顯示主視窗": "Show main window",
    "開啟系統清理": "Open System Cleaner",
    "硬碟健康檢查": "Disk health check",
    "刷新 DNS": "Flush DNS",
    "釋放記憶體": "Free memory",
    "重新掃描磁碟儲存": "Rescan disk storage",
    "UAC 提權（背景）": "UAC elevation (background)",
    "顯示日志控制台": "Show log console",
    "開啟日誌資料夾": "Open logs folder",
    "開機自動啟動": "Run at startup",
    "退出": "Quit",
    "管理員": "Administrator",
    "管理員代理": "Admin broker",
    "標準使用者": "Standard user",
    "看門狗運行中": "Watchdog running",
    "看門狗未運行": "Watchdog not running",
    "安全": "Safe",
    "玩家": "Gaming",
    "辦公": "Office",
    "極簡": "Minimal",
    "開發者": "Developer",
    "平衡": "Balanced",
    "高效能": "High performance",
    "終極效能": "Ultimate performance",
    "省電": "Power saver",
    "未知": "Unknown",
    "使用中": "Active",
    "準備中…": "Preparing…",
    "完成": "Done",
    "詳細": "Details",
    "掃描": "Scan",
    "尚未掃描": "Not scanned yet",
    "啟用": "On",
    "停用": "Off",
    "是": "Yes",
    "否": "No",
}

JA_PHRASES = {
    "優化掃描": "最適化スキャン",
    "套用": "適用",
    "工作管理員": "タスクマネージャー",
    "系統清理": "システムクリーナー",
    "清理歷史": "クリーン履歴",
    "硬碟健康": "ディスク健康",
    "磁碟分析": "ディスク分析",
    "執行最佳化": "最適化を実行",
    "進行中": "進行中",
    "已最佳化": "最適化済み",
    "狀況良好": "良好",
    "需要最佳化": "最適化が必要",
    "取消": "キャンセル",
    "確定": "OK",
    "刪除": "削除",
    "關閉": "閉じる",
    "語言": "言語",
    "詳細": "詳細",
    "掃描": "スキャン",
    "未知": "不明",
    "普通": "標準",
    "容量": "容量",
    "強制 DoH": "DoH を強制",
    "百度": "バイドゥ",
    "自動目的地": "自動宛先",
    "速度曲線": "スピード曲線",
    "非計量": "非従量制",
    "進行中": "進行中",
    "更新中…": "更新中…",
    "準備中…": "準備中…",
}


def esc(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def decode(raw: str) -> str:
    return (
        raw.replace("\\n", "\n")
        .replace("\\t", "\t")
        .replace('\\"', '"')
        .replace("\\\\", "\\")
    )


_GLOSSARY_ORDER = sorted(
    (k for k in EN_GLOSSARY.keys() if len(k) >= 2),
    key=len,
    reverse=True,
)

_BATCH_EN_PHRASES: dict[str, str] = {}
_BATCH_JA_PHRASES: dict[str, str] = {}


def _load_batch_phrases(path: Path, target: dict[str, str]) -> None:
    if not path.exists():
        return
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            target.clear()
            target.update({k: v for k, v in data.items() if isinstance(k, str) and isinstance(v, str)})
    except Exception:
        pass


def _load_batch_en_phrases() -> None:
    _load_batch_phrases(ROOT / "tools" / "en_phrases_batch.json", _BATCH_EN_PHRASES)


def _load_batch_ja_phrases() -> None:
    _load_batch_phrases(ROOT / "tools" / "ja_phrases_batch.json", _BATCH_JA_PHRASES)


_load_batch_en_phrases()
_load_batch_ja_phrases()


def apply_en_glossary(s: str) -> str:
    out = s
    for zh in _GLOSSARY_ORDER:
        if zh in out:
            out = out.replace(zh, EN_GLOSSARY[zh])
    out = re.sub(r"  +", " ", out)
    out = re.sub(r"\s+([,.;:!?])", r"\1", out)
    return out.strip()


def to_en(s: str) -> str:
    if s in EN_PHRASES:
        return EN_PHRASES[s]
    if s in _BATCH_EN_PHRASES:
        return _BATCH_EN_PHRASES[s]
    if not re.search(r"[\u4e00-\u9fff]", s):
        return s
    translated = apply_en_glossary(s)
    if not re.search(r"[\u4e00-\u9fff]", translated):
        return translated
    return translated


def to_ja(s: str) -> str:
    if s in JA_PHRASES:
        return JA_PHRASES[s]
    if s in _BATCH_JA_PHRASES:
        return _BATCH_JA_PHRASES[s]
    if not re.search(r"[\u4e00-\u9fff]", s):
        return s
    return s


def collect_from_file(path: Path, bag: dict[str, bool]) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    for rx in I18N_PATTERNS:
        for m in rx.finditer(text):
            bag.setdefault(decode(m.group(1)), True)
    for m in STR_RE.finditer(text):
        raw = m.group(1) if m.group(1) is not None else m.group(2)
        if raw is None:
            continue
        s = decode(raw)
        if not re.search(r"[\u4e00-\u9fff]", s):
            continue
        if len(s) > 220 or s.startswith("##"):
            continue
        ctx = text[max(0, m.start() - 48) : m.start()]
        if "HLOG_" in ctx or "SPDLOG_" in ctx:
            continue
        if "I18N(" in ctx or "W18N(" in ctx or "HTR(" in ctx:
            continue
        if "REG_PAGEN" in ctx or "RegisterClean" in ctx:
            continue
        bag.setdefault(s, True)


def main() -> None:
    unique: dict[str, bool] = {}
    for p in sorted(ROOT.rglob("*.cpp")):
        if any(part in SKIP_DIRS for part in p.parts):
            continue
        if p.name in SKIP_FILES:
            continue
        collect_from_file(p, unique)

    rows = sorted(unique.keys())
    lines = [
        '#include "Hi18nBuiltinPages.h"',
        "",
        "namespace Hi18nBuiltinPages {",
        "\tstatic const Row kTable[] = {",
    ]
    for s in rows:
        zt = esc(s)
        zc = esc(s.translate(TC2SC))
        en = esc(to_en(s))
        ja = esc(to_ja(s))
        lines.append(f'\t\t{{ "{zt}", "{zc}", "{en}", "{ja}" }},')
    lines += [
        "\t};",
        "",
        "\tconst Row* Table() { return kTable; }",
        "\tint Count() { return static_cast<int>(sizeof(kTable) / sizeof(kTable[0])); }",
        "} // namespace Hi18nBuiltinPages",
        "",
    ]
    safe_write_text(ROOT / "Hi18nBuiltinPages.cpp", "\n".join(lines))
    safe_write_text(ROOT / "tools" / "i18n_extracted.json", json.dumps(rows, ensure_ascii=False, indent=2))
    samples_dir = ROOT / "i18n_samples"
    samples_dir.mkdir(parents=True, exist_ok=True)
    en_pack = {
        "language": "en-US",
        "name": "English",
        "strings": {s: to_en(s) for s in rows if re.search(r"[\u4e00-\u9fff]", s)},
    }
    (samples_dir / "en-US.json").write_text(
        json.dumps(en_pack, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    zh_pack = {
        "language": "zh-CN",
        "name": "简体中文",
        "strings": {s: s.translate(TC2SC) for s in rows if re.search(r"[\u4e00-\u9fff]", s)},
    }
    (samples_dir / "zh-CN.json").write_text(
        json.dumps(zh_pack, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    ja_pack = {
        "language": "ja-JP",
        "name": "日本語",
        "strings": {s: to_ja(s) for s in rows if re.search(r"[\u4e00-\u9fff]", s)},
    }
    (samples_dir / "ja-JP.json").write_text(
        json.dumps(ja_pack, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    en_done = sum(1 for s in rows if not re.search(r"[\u4e00-\u9fff]", to_en(s)))
    ja_done = sum(
        1
        for s in rows
        if re.search(r"[\u4e00-\u9fff]", s) and to_ja(s) != s
    )
    print(
        f"Hi18nBuiltinPages: {len(rows)} strings "
        f"(EN full={en_done}, JA full={ja_done})"
    )


if __name__ == "__main__":
    main()

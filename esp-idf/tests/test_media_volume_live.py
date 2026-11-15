#!/usr/bin/env python3
"""Regression guard: Mira7 media volume is live but bounded, final value wins."""
from pathlib import Path
ui=(Path(__file__).resolve().parents[1]/"main"/"mira_full.c").read_text(encoding="utf-8")
start=ui.index("static void media_direct_action(")
end=ui.index("static void media_add_button(",start)
volume=ui[start:end]
assert "code!=LV_EVENT_VALUE_CHANGED&&code!=LV_EVENT_RELEASED" in volume
assert "code==LV_EVENT_PRESSED||code==LV_EVENT_PRESSING" in volume
assert "w->live_last_value==volume" in volume
assert "now-w->live_last_tick)<150u" in volume
assert "if(code==LV_EVENT_VALUE_CHANGED){" in volume
assert "lv_label_set_text(w->fields[5],shown)" in volume
assert "suffix=\"volume_cmd\"" in volume
assert "w->live_last_tick=lv_tick_get()" in volume
assert "650u:350u" in volume
events=ui[ui.index("const uintptr_t vol="):ui.index("if(!strcmp(type,\"camera\"))",ui.index("const uintptr_t vol="))]
for event in ["PRESSED","PRESSING","VALUE_CHANGED","RELEASED"]:
    assert "LV_EVENT_"+event in events,event
assert "mira_beta_user_action(command,(action>=3)?" in volume
print("PASS Mira7 volume updates live every >=150ms and releases final value")

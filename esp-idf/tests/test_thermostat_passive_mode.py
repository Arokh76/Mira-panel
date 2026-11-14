#!/usr/bin/env python3
"""Static regression contracts for passive thermostat feedback and French modes."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]/"main"
http=(root/"mira_jeedom.c").read_text(encoding="utf-8")
ui=(root/"mira_full.c").read_text(encoding="utf-8")
assert '"listvalue","color","button"' in http
segment=http[http.index("static esp_err_t updatenr("):http.index("// Provision the existing Jeedom screen password")]
assert 'const bool list_by_value=!strcmp(selected,"listvalue");' in segment
assert 'strcmp(jstr((cJSON*)entry,"nomliste",""),action_id)' in segment
assert 'strcmp(jstr((cJSON*)entry,"nom",""),value)' in segment
assert 'success=mira_full_enqueue_state' not in segment  # plain queue call guarded below
assert 'if(!mira_full_enqueue_state(name,value))' in segment
assert 'mira_jeedom_queue_local_action' not in segment
assert 'mira_other_transport' not in segment
assert 'lv_obj_add_event_cb(w->thermostat_mode_select,media_dropdown_opened,' in ui
assert 'mira_media_display_text(first,normalized,sizeof(normalized));' in ui
assert '"E\\xCC\\x81","\\xC3\\x89"' in ui
assert 'mira_media_display_text(msg->value,normalized_mode,sizeof(normalized_mode));' in ui
print("PASS passive thermostat listvalue mode + accented French popup")

#!/usr/bin/env python3
"""Google Cast / YouTube Music album art must accept exact public Google CDN hosts."""
from pathlib import Path
import re
source=(Path(__file__).resolve().parents[1]/"main"/"mira_album_art.c").read_text(encoding="utf-8")
allowlist=source[source.index("static bool cover_url("):source.index("static void cover_worker(")]
hosts=set(re.findall(r'"https://([^/]+/[^"]*)"',allowlist))
required={
    "i.scdn.co/image/", "mosaic.scdn.co/",
    "lh3.googleusercontent.com/", "lh4.googleusercontent.com/",
    "lh5.googleusercontent.com/", "lh6.googleusercontent.com/",
    "i.ytimg.com/", "yt3.ggpht.com/", "yt3.googleusercontent.com/",
}
assert required.issubset(hosts),(required-hosts)
assert '"http://' not in allowlist
assert '"https://googleusercontent.com/"' not in allowlist
assert '"https://ytimg.com/"' not in allowlist
assert 'strncmp(url,allowed[i],n)&&url[n]' in allowlist
assert 'size_t length=strlen(url);' in allowlist
assert 'length>MIRA_COVER_URL' in allowlist
print("PASS strict HTTPS Google Cast and YouTube Music cover CDN allowlist")

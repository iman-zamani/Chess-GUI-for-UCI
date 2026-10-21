#!/usr/bin/env python3
"""Moves the CSS class rules of an SVG's <style> blocks into style="" attributes,
so renderers without CSS support (nanosvg) draw it the same.

  inline_svg_css.py IN.svg OUT.svg
"""
import re, sys

src = open(sys.argv[1]).read()
rules = {}
for block in re.findall(r"<style[^>]*>(.*?)</style>", src, re.S):
    for sel, body in re.findall(r"([^{}]+)\{([^}]*)\}", block):
        for s in sel.split(","):
            s = s.strip()
            if s.startswith("."):
                rules[s[1:]] = rules.get(s[1:], "") + body.strip().rstrip(";") + ";"
src = re.sub(r"<style[^>]*>.*?</style>", "", src, flags=re.S)


def inline(m):
    tag = m.group(0)
    cm = re.search(r'\sclass="([^"]*)"', tag)
    if not cm:
        return tag
    css = "".join(rules.get(c, "") for c in cm.group(1).split())
    tag = tag.replace(cm.group(0), "")
    sm = re.search(r'\sstyle="([^"]*)"', tag)
    if sm:   # the element's own style wins over its classes
        return tag.replace(sm.group(0), ' style="%s%s"' % (css, sm.group(1)))
    return re.sub(r"^<(\w+)", lambda t: '<%s style="%s"' % (t.group(1), css), tag)


src = re.sub(r"<[a-zA-Z][^>]*>", inline, src)
open(sys.argv[2], "w").write(src)

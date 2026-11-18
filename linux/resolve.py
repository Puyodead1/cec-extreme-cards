#!/usr/bin/env python3
"""Resolve a package set against a yum repodata/primary.xml.gz and print the RPM file names.

Usage: python3 -I resolve.py <primary.xml.gz> pkg [pkg ...]

Picks the newest i386/i686/noarch build when a name has several. Requirements are matched
against <rpm:provides> entries and against the file lists in primary.xml (which only holds
/etc, /usr/bin, /bin, /sbin and similar paths; anything else is reported as unresolved).
"""
import gzip
import sys
import xml.etree.ElementTree as ET

NS = {"c": "http://linux.duke.edu/metadata/common", "rpm": "http://linux.duke.edu/metadata/rpm"}
ARCH_PREF = {"i686": 3, "i586": 2, "i386": 1, "noarch": 0}


def evr_key(v):
    out = []
    for part in (v.get("epoch") or "0", v.get("ver") or "", v.get("rel") or ""):
        out.append(tuple(int(x) if x.isdigit() else x for x in part.replace("_", ".").split(".")))
    return out


def load(path):
    pkgs = {}
    for p in ET.parse(gzip.open(path)).getroot().findall("c:package", NS):
        arch = p.findtext("c:arch", namespaces=NS)
        if arch not in ARCH_PREF:
            continue
        name = p.findtext("c:name", namespaces=NS)
        fmt = p.find("c:format", NS)
        rec = {
            "name": name,
            "arch": arch,
            "evr": evr_key(p.find("c:version", NS).attrib),
            "href": p.find("c:location", NS).get("href"),
            "provides": [e.get("name") for e in fmt.findall("rpm:provides/rpm:entry", NS)],
            "requires": [e.get("name") for e in fmt.findall("rpm:requires/rpm:entry", NS)],
            "files": [f.text for f in fmt.findall("c:file", NS)],
        }
        old = pkgs.get(name)
        if old is None or (rec["evr"], ARCH_PREF[arch]) > (old["evr"], ARCH_PREF[old["arch"]]):
            pkgs[name] = rec
    return pkgs


def main():
    pkgs = load(sys.argv[1])
    prov = {}
    for p in pkgs.values():
        for x in p["provides"] + p["files"] + [p["name"]]:
            prov.setdefault(x, p["name"])
    want, done, missing = list(sys.argv[2:]), set(), set()
    while want:
        n = want.pop()
        if n in done:
            continue
        if n not in pkgs:
            missing.add(n)
            continue
        done.add(n)
        for r in pkgs[n]["requires"]:
            if r.startswith("rpmlib("):
                continue
            owner = prov.get(r)
            if owner is None:
                missing.add(r)
            elif owner not in done:
                want.append(owner)
    for n in sorted(done):
        print(pkgs[n]["href"])
    for m in sorted(missing):
        print("UNRESOLVED", m, file=sys.stderr)


if __name__ == "__main__":
    main()

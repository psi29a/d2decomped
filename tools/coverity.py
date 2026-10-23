#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""List, read and triage Coverity Scan issues through Coverity Connect's REST v2.

  tools/coverity.py list [--impact High|Medium|Low] [--all]   CID, impact, type, file:line, function
  tools/coverity.py show CID [CID ...]                         each issue's columns, and its source line
  tools/coverity.py triage CID [CID ...] --as fp|intentional|bug --comment TEXT

Auth, first that's set:
  - an authentication key in the macOS Keychain (scan8: user menu, Authentication Keys):
      security add-generic-password -s coverity-scan -a SCAN_USERNAME -w
  - COVERITY_USER + COVERITY_KEY in the environment
  - the authentication key file Coverity downloads (JSON with username and key),
    at COVERITY_KEY_FILE or ~/.coverity.key
  - the browser's session: COVERITY_SESSION (cookie COVJSESSIONID-build) + COVERITY_XSRF
    (cookie XSRF-TOKEN), from devtools; expires about an hour idle
COVERITY_HOST (scan8.scan.coverity.com) and COVERITY_PROJECT (psi29a-d2decomped) override.
"""
import argparse
import base64
import json
import os
import pathlib
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

HOST = os.environ.get("COVERITY_HOST", "scan8.scan.coverity.com")
PROJECT = os.environ.get("COVERITY_PROJECT", "psi29a-d2decomped")
REPO = pathlib.Path(__file__).resolve().parent.parent
COLUMNS = ["cid", "displayImpact", "displayType", "displayFile", "lineNumber", "displayFunction", "status", "classification"]
CLASSIFICATIONS = {"fp": "False Positive", "intentional": "Intentional", "bug": "Bug"}


def auth_headers() -> dict[str, str]:
    user, key = os.environ.get("COVERITY_USER"), os.environ.get("COVERITY_KEY")
    if not (user and key) and sys.platform == "darwin":
        found = subprocess.run(["security", "find-generic-password", "-s", "coverity-scan", "-g"], capture_output=True, text=True)
        account = re.search(r'"acct"<blob>="([^"]*)"', found.stdout)
        password = re.search(r'^password: "(.*)"$', found.stderr, re.M)
        if account and password:
            user, key = account.group(1), password.group(1)
    key_file = pathlib.Path(os.environ.get("COVERITY_KEY_FILE", pathlib.Path.home() / ".coverity.key"))
    if not (user and key) and key_file.is_file():
        try:                                                  # unreadable or not the key's JSON: on to the session
            stored = json.loads(key_file.read_text())
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            print(f"coverity: {key_file} unusable ({error}); trying COVERITY_SESSION", file=sys.stderr)
            stored = {}
        if isinstance(stored, dict) and stored.get("username") and stored.get("key"):
            user, key = stored["username"], stored["key"]
    if user and key:
        return {"Authorization": "Basic " + base64.b64encode(f"{user}:{key}".encode()).decode()}
    session, xsrf = os.environ.get("COVERITY_SESSION"), os.environ.get("COVERITY_XSRF")
    if session and xsrf:
        return {"Cookie": f"isAuthenticated=true; COVJSESSIONID-build={session}; XSRF-TOKEN={xsrf}", "X-XSRF-TOKEN": xsrf}
    sys.exit("no Coverity credentials: see tools/coverity.py --help")


def call(method: str, path: str, params: dict | None = None, body: dict | None = None) -> dict:
    url = f"https://{HOST}{path}" + ("?" + urllib.parse.urlencode(params) if params else "")
    request = urllib.request.Request(url, method=method, data=json.dumps(body).encode() if body is not None else None,
                                     headers={"Accept": "application/json", "Content-Type": "application/json", "User-Agent": "d2d-coverity/1", **auth_headers()})
    try:
        with urllib.request.urlopen(request) as response:
            text = response.read().decode()
    except urllib.error.HTTPError as error:
        sys.exit(f"{method} {path}: HTTP {error.code}: {error.read().decode()[:400]}")
    if text.lstrip().startswith("<"):                          # a login page: the redirect was followed
        sys.exit(f"{method} {path}: got HTML, not JSON (credentials refused or session expired)")
    return json.loads(text) if text.strip() else {}


def search(cids: list[int] | None = None) -> list[dict[str, str]]:
    filters = [{"columnKey": "project", "matchMode": "oneOrMoreMatch",
                "matchers": [{"class": "Project", "name": PROJECT, "type": "nameMatcher"}]}]
    if cids:
        filters.append({"columnKey": "cid", "matchMode": "oneOrMoreMatch",
                        "matchers": [{"type": "idMatcher", "id": str(cid)} for cid in cids]})
    rows, offset = [], 0
    while True:
        page = call("POST", "/api/v2/issues/search",
                    {"locale": "en_us", "offset": offset, "rowCount": 200, "includeColumnLabels": "true", "queryType": "bySnapshot"},
                    {"filters": filters, "columns": COLUMNS, "snapshotScope": {"show": {"scope": "last()", "includeOutdatedSnapshots": False}}})
        rows += [{cell["key"]: cell["value"] for cell in row} for row in page.get("rows", [])]
        offset += 200
        if offset >= page.get("totalRows", 0):
            return rows


def triage_store() -> str:
    projects = call("GET", f"/api/v2/projects/{urllib.parse.quote(PROJECT, safe='')}", {"includeStreams": "true", "locale": "en_us"})
    stream = projects["projects"][0]["streams"][0]["name"]
    streams = call("GET", f"/api/v2/streams/{urllib.parse.quote(stream, safe='')}", {"locale": "en_us"})
    return streams["streams"][0]["triageStoreName"]


def source_line(file: str, line: str) -> str:
    try:
        return (REPO / file.lstrip("/")).read_text().splitlines()[int(line) - 1].strip()
    except (OSError, ValueError, IndexError):
        return "(not in this checkout)"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    listing = commands.add_parser("list")
    listing.add_argument("--impact")
    listing.add_argument("--all", action="store_true", help="triaged ones too")
    show = commands.add_parser("show")
    show.add_argument("cids", type=int, nargs="+")
    triage = commands.add_parser("triage")
    triage.add_argument("cids", type=int, nargs="+")
    triage.add_argument("--as", dest="classification", choices=CLASSIFICATIONS, required=True)
    triage.add_argument("--comment", required=True)
    args = parser.parse_args()

    if args.command == "list":
        for row in search():
            if args.impact and row["displayImpact"] != args.impact: continue
            if not args.all and row.get("classification") not in (None, "", "Unclassified"): continue
            print(f'{row["cid"]}  {row["displayImpact"]:<6} {row["displayType"]:<45} {row["displayFile"]}:{row["lineNumber"]}  {row["displayFunction"]}')
    elif args.command == "show":
        for row in search(args.cids):
            print(json.dumps(row, indent=2))
            print("   ", source_line(row["displayFile"], row["lineNumber"]))
    else:
        call("PUT", "/api/v2/issues/triage", {"triageStoreName": triage_store()},
             {"cids": args.cids, "attributeValuesList": [{"attributeName": "Classification", "attributeValue": CLASSIFICATIONS[args.classification]},
                                                         {"attributeName": "Comment", "attributeValue": args.comment}]})
        print(f"{len(args.cids)} marked {CLASSIFICATIONS[args.classification]}")


if __name__ == "__main__":
    main()

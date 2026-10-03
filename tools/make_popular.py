#!/usr/bin/env python3
"""Builds the popularity lists (sdcard/TriMux/share/popular/<SYS>.txt).

Each list is the platform's best-selling games, most sold first, taken from
the sales tables of English Wikipedia articles ("List of best-selling ...").
Sales figures are facts; the selection and order come from Wikipedia
(CC BY-SA 4.0), so every file names its source article and license.

Usage (the wikitext is downloaded once, by hand, then kept out of git):

    python3 tools/make_popular.py <folder with Page_title.wiki files>

A line in the output may carry alternative titles separated by '|'
(regional names, or one entry that covers two games such as Pokemon Red and
Blue), so the menu can recognise either file name.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "sdcard", "TriMux", "share", "popular")
RETRIEVED = "2026-10-03"

# system id -> Wikipedia article
SOURCES = {
    "FC": "List_of_best-selling_Nintendo_Entertainment_System_video_games",
    "SFC": "List_of_best-selling_Super_Nintendo_Entertainment_System_video_games",
    "GB": "List_of_best-selling_Game_Boy_video_games",
    "GBA": "List_of_best-selling_Game_Boy_Advance_video_games",
    "MD": "List_of_best-selling_Sega_Genesis_video_games",
    "PS": "List_of_best-selling_PlayStation_video_games",
    "N64": "List_of_best-selling_Nintendo_64_video_games",
    "PSP": "List_of_best-selling_PlayStation_Portable_video_games",
}

# Entries that stand for more than one game, or whose cartridge is named
# differently (No-Intro names). Keys are the Wikipedia titles after cleanup.
ALIASES = {
    "Pokémon Red, Green, Blue and Yellow": ["Pokemon - Red Version", "Pokemon - Blue Version",
                                            "Pokemon - Yellow Version - Special Pikachu Edition",
                                            "Pocket Monsters - Aka", "Pocket Monsters - Midori",
                                            "Pocket Monsters - Ao", "Pocket Monsters - Pikachu"],
    "Pokémon Red and Blue": ["Pokemon - Red Version", "Pokemon - Blue Version", "Pocket Monsters - Aka",
                             "Pocket Monsters - Midori", "Pocket Monsters - Ao"],
    "Pokémon Gold and Silver": ["Pokemon - Gold Version", "Pokemon - Silver Version", "Pocket Monsters - Kin",
                                "Pocket Monsters - Gin"],
    "Pokémon Yellow": ["Pokemon - Yellow Version - Special Pikachu Edition", "Pocket Monsters - Pikachu"],
    "Pokémon Crystal": ["Pokemon - Crystal Version", "Pocket Monsters - Crystal Version"],
    "Pokémon Ruby and Sapphire": ["Pokemon - Ruby Version", "Pokemon - Sapphire Version",
                                  "Pocket Monsters - Ruby", "Pocket Monsters - Sapphire"],
    "Pokémon FireRed and LeafGreen": ["Pokemon - FireRed Version", "Pokemon - LeafGreen Version",
                                      "Pocket Monsters - FireRed", "Pocket Monsters - LeafGreen"],
    "Pokémon Emerald": ["Pokemon - Emerald Version", "Pocket Monsters - Emerald"],
    "Pokémon Pinball": ["Pokemon Pinball"],
    "Super Mario Bros. / Duck Hunt": ["Super Mario Bros. + Duck Hunt", "Super Mario Bros."],
    "Super Mario Bros.": ["Super Mario Bros."],
    "Super Mario Land 2: 6 Golden Coins": ["Super Mario Land 2 - 6 Golden Coins"],
    "Sonic the Hedgehog 2": ["Sonic The Hedgehog 2"],
    "Sonic the Hedgehog": ["Sonic The Hedgehog"],
    "Super Mario Advance": ["Super Mario Advance - Super Mario USA + Mario Brothers"],
    "Super Mario Advance 2: Super Mario World": ["Super Mario Advance 2 - Super Mario World"],
    "Super Mario Advance 4: Super Mario Bros. 3": ["Super Mario Advance 4 - Super Mario Bros. 3"],
    "Pokémon Red, Green and Blue": ["Pokemon - Red Version", "Pokemon - Blue Version", "Pocket Monsters - Aka",
                                    "Pocket Monsters - Midori", "Pocket Monsters - Ao"],
    "Pokémon Trading Card Game": ["Pokemon Trading Card Game"],
    "Pokémon Pinball: Ruby & Sapphire": ["Pokemon Pinball - Ruby & Sapphire"],
    "Pokémon Mystery Dungeon: Red Rescue Team": ["Pokemon Mystery Dungeon - Red Rescue Team"],
    "The Legend of Zelda: Oracle of Seasons and Oracle of Ages": ["Legend of Zelda, The - Oracle of Seasons",
                                                                  "Legend of Zelda, The - Oracle of Ages"],
    "Super Mario World: Super Mario Advance 2": ["Super Mario Advance 2 - Super Mario World"],
    "Yoshi's Island: Super Mario Advance 3": ["Super Mario Advance 3 - Yoshi's Island"],
    ("GBA", "The Legend of Zelda: A Link to the Past"): ["Legend of Zelda, The - A Link to the Past & Four Swords"],
    # North American names of the early Dragon Quest and Final Fantasy games
    ("FC", "Dragon Quest"): ["Dragon Warrior"],
    ("FC", "Dragon Quest II"): ["Dragon Warrior II"],
    ("FC", "Dragon Quest III"): ["Dragon Warrior III"],
    ("FC", "Dragon Quest IV"): ["Dragon Warrior IV"],
    ("FC", "Punch-Out!!"): ["Mike Tyson's Punch-Out!!", "Punch-Out!! Featuring Mr. Dream"],
    ("SFC", "Final Fantasy VI"): ["Final Fantasy III"],
    ("SFC", "Final Fantasy IV"): ["Final Fantasy II"],
    ("SFC", "Super Mario RPG"): ["Super Mario RPG - Legend of the Seven Stars"],
    ("SFC", "Secret of Mana"): ["Seiken Densetsu 2"],
    ("SFC", "Kirby Super Star"): ["Kirby's Fun Pak", "Hoshi no Kirby Super Deluxe"],
    ("SFC", "Super Mario Kart"): ["Super Mario Kart"],
    ("MD", "Sonic the Hedgehog 2"): ["Sonic The Hedgehog 2"],
    ("N64", "Star Fox 64"): ["Lylat Wars"],
    ("PS", "Harry Potter and the Philosopher's Stone"): ["Harry Potter and the Sorcerer's Stone"],
    ("PS", "Everybody's Golf"): ["Hot Shots Golf", "Minna no Golf"],
    ("PS", "Resident Evil"): ["Biohazard"],
    ("PS", "Resident Evil 2"): ["Biohazard 2"],
    ("PS", "Resident Evil 3: Nemesis"): ["Biohazard 3 - Last Escape"],
}


def strip_markup(s):
    s = re.sub(r"<ref[^>]*/>", "", s)
    s = re.sub(r"<ref[^>]*>.*?</ref>", "", s, flags=re.S)
    s = re.sub(r"\{\{efn[^{}]*\}\}", "", s)
    s = re.sub(r"\{\{nts\|([^{}|]*)\}\}", r"\1", s)  # {{nts|7.5}}{{nbsp}}million (PSP page)
    s = s.replace("{{nbsp}}", " ")
    s = re.sub(r"\{\{(?:sort|sortname)\|[^|{}]*\|([^{}]*)\}\}", r"\1", s)
    s = re.sub(r"\[\[(?:[^|\]]*\|)?([^\]]*)\]\]", r"\1", s)
    s = re.sub(r"\{\{[^{}]*\}\}", "", s)
    s = re.sub(r"<[^>]+>", "", s)
    s = s.replace("''", "").replace("&nbsp;", " ")
    s = re.sub(r"[†‡*#]", "", s)
    return re.sub(r"\s+", " ", s).strip()


def cells(row):
    """Cells of a wikitable row (one per line or '||' separated)."""
    out = []
    for line in row.split("\n"):
        line = line.strip()
        if not line or line[0] not in "|!" or line.startswith("|}") or line.startswith("|+"):
            continue
        sep = r"!!" if line[0] == "!" else r"\|\|"  # "Punch-Out!!" is a title, not a separator
        for part in re.split(sep, line[1:]):
            # drop "attr=value |" prefixes (style, bgcolor, scope ...)
            m = re.match(r'^\s*(?:[a-z-]+\s*=\s*"[^"]*"\s*)+\|(?!\|)(.*)$', part, flags=re.S)
            if m:
                part = m.group(1)
            m = re.match(r"^\s*(?:scope|style|bgcolor|align|rowspan|colspan)=[^|]*\|(.*)$", part, flags=re.S)
            if m:
                part = m.group(1)
            out.append(part.strip())
    return out


SALES = re.compile(r"(\d{1,3}(?:,\d{3})+|\d+(?:\.\d+)?\s*million)")


def parse_sales(text):
    best = 0
    for m in SALES.finditer(text.replace(" ", " ")):
        v = m.group(1)
        n = float(v.split()[0]) * 1_000_000 if "million" in v else int(v.replace(",", ""))
        best = max(best, int(n))
    return best


def parse(wikitext):
    games = []
    for table in re.findall(r"\{\|\s*class=\"wikitable[^\n]*sortable.*?\n\|\}", wikitext, flags=re.S):
        for row in table.split("\n|-")[1:]:
            c = cells(row)
            if len(c) < 3:
                continue
            title_raw = next((x for x in c if "''" in x or "[[" in x), c[0])
            title = strip_markup(title_raw)
            sales = max((parse_sales(strip_markup(x)) for x in c[1:] if not re.search(r"\{\{dts", x)), default=0)
            # "(international version)", "(Japanese version)": same game file name
            title = re.sub(r"\s*\((?:international|japanese|north american|original) version\)$", "", title, flags=re.I)
            if re.match(r"Pok[eé]mon Generation", title):
                continue  # series totals, not a game
            if title and sales >= 100_000:
                games.append((sales, title))
    seen, out = set(), []
    for sales, title in sorted(games, key=lambda g: -g[0]):
        if title not in seen:
            seen.add(title)
            out.append((sales, title))
    return out


def main():
    src = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    for sysid, page in SOURCES.items():
        path = os.path.join(src, page + ".wiki")
        if not os.path.exists(path):
            print("missing", path)
            continue
        games = parse(open(path, encoding="utf-8").read())
        if not games:
            print(sysid, "no table found, skipped")
            continue
        with open(os.path.join(OUT, sysid + ".txt"), "w", encoding="utf-8") as f:
            f.write("# Best-selling games, most sold first (%d entries).\n" % len(games))
            f.write("# Source: https://en.wikipedia.org/wiki/%s (retrieved %s),\n" % (page, RETRIEVED))
            f.write("# CC BY-SA 4.0, by Wikipedia contributors. Generated by tools/make_popular.py.\n")
            f.write("# One game per line; '|' separates alternative titles.\n")
            for _, title in games:
                alts = ALIASES.get((sysid, title), []) + ALIASES.get(title, [])
                if title.startswith("Disney's "):
                    alts.append(title[len("Disney's "):])  # cartridges say "Aladdin"
                f.write("|".join([title] + alts) + "\n")
        print(sysid, len(games), [t for _, t in games[:5]])


if __name__ == "__main__":
    main()

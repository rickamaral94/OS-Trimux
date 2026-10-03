"""TriMux tools (Aplicativos › Ferramentas): statistics, surprise game, card
cleanup, file manager and the browser file server.

The file server is exercised here through its CGI program (trimuxctl webcgi)
with the environment BusyBox httpd would give it; the real httpd of the
firmware is only run in the emulated check described in docs/TESTES.md. No
test here touches a real device or network."""
import os
import subprocess

import pytest

from conftest import CTL, ctl, read, write
from test_ui import _ui_runs, ui

needs_ui = pytest.mark.skipif(not _ui_runs(), reason="build/native/trimux-ui not built for this system")
HOST = "192.168.0.7:8080"


def cgi(env, query, method="GET", body=b"", referer="http://%s/" % HOST):
    e = dict(env, REQUEST_METHOD=method, QUERY_STRING=query, HTTP_HOST=HOST)
    if referer:
        e["HTTP_REFERER"] = referer
    if method == "POST":
        e["CONTENT_LENGTH"] = str(len(body))
    r = subprocess.run([CTL, "webcgi"], env=e, input=body, capture_output=True, timeout=60)
    head, _, payload = r.stdout.partition(b"\r\n\r\n")
    status = int(head.split(b" ")[1])
    return status, head.decode(), payload


# ---------------------------------------------------------------- browser file server

def test_web_list_and_download(env, card):
    status, _, body = cgi(env, "op=list&path=Roms%2FGBA")
    assert status == 200
    import json
    j = json.loads(body)
    names = [e["n"] for e in j["entries"]]
    assert "Celeste Classic (World).gba" in names and j["readonly"] is False and j["total"] > 0
    write(os.path.join(card, "Roms/GBA/Celeste Classic (World).gba"), "ROMDATA")
    status, head, body = cgi(env, "op=get&path=Roms/GBA/Celeste%20Classic%20(World).gba")
    assert status == 200 and body == b"ROMDATA"
    assert "filename*=UTF-8''Celeste%20Classic%20%28World%29.gba" in head
    root = json.loads(cgi(env, "op=list&path=")[2])
    assert {"n": "TriMux"}.items() <= next(e for e in root["entries"] if e["n"] == "TriMux").items()
    assert next(e for e in root["entries"] if e["n"] == "TriMux").get("ro") == 1


def test_web_refuses_paths_outside_the_card(env):
    for q in ("op=list&path=..", "op=list&path=Roms/../..", "op=get&path=%2Fetc%2Fpasswd/..",
              "op=get&path=Roms%2F..%2F..%2Fetc%2Fpasswd", "op=list&path=Roms/%0a"):
        assert cgi(env, q)[0] in (400, 404), q
    # an absolute path is read as relative to the card
    assert cgi(env, "op=get&path=/etc/passwd")[0] == 404


def test_web_upload_mkdir_delete(env, card):
    data = os.urandom(300000)
    st, _, body = cgi(env, "op=put&path=Roms/GBA&name=New%20Game.gba", "POST", data)
    assert st == 200, body
    with open(os.path.join(card, "Roms/GBA/New Game.gba"), "rb") as f:
        assert f.read() == data
    assert not [n for n in os.listdir(os.path.join(card, "Roms/GBA")) if n.endswith(".trimux-part")]
    # no silent overwrite
    st, _, body = cgi(env, "op=put&path=Roms/GBA&name=New%20Game.gba", "POST", b"other")
    assert st == 409 and b"exists" in body
    assert cgi(env, "op=put&path=Roms/GBA&name=New%20Game.gba&overwrite=1", "POST", b"other")[0] == 200
    assert read(os.path.join(card, "Roms/GBA/New Game.gba")) == "other"
    assert cgi(env, "op=mkdir&path=Roms&name=N64", "POST")[0] == 200
    assert os.path.isdir(os.path.join(card, "Roms/N64"))
    assert cgi(env, "op=del&path=Roms/GBA/New%20Game.gba", "POST")[0] == 200
    assert not os.path.exists(os.path.join(card, "Roms/GBA/New Game.gba"))
    # folders only when empty
    assert cgi(env, "op=del&path=Roms/GBA", "POST")[0] == 409
    assert cgi(env, "op=del&path=Roms/N64", "POST")[0] == 200
    assert "web: uploaded Roms/GBA/New Game.gba" in read(os.path.join(card, "TriMuxData/logs/trimux.log"))


def test_web_never_writes_system_folders(env, card):
    assert cgi(env, "op=put&path=TriMux/bin&name=x", "POST", b"x")[0] == 403
    assert cgi(env, "op=put&path=&name=TriMux", "POST", b"x")[0] == 400
    assert cgi(env, "op=mkdir&path=&name=trimui", "POST")[0] == 400
    assert cgi(env, "op=del&path=TriMux/VERSION", "POST")[0] == 403
    assert cgi(env, "op=del&path=trimux/VERSION", "POST")[0] == 403      # FAT ignores case
    assert os.path.exists(os.path.join(card, "TriMux/VERSION"))


def test_web_changes_need_the_page_itself(env, card):
    """GET can never change anything, and a POST from another site (wrong or
    missing Referer) is refused."""
    assert cgi(env, "op=del&path=Roms/GBA/Celeste%20Classic%20(World).gba")[0] == 405
    for ref in (None, "http://evil.example/", "http://%s.evil/" % HOST):
        st = cgi(env, "op=del&path=Roms/GBA/Celeste%20Classic%20(World).gba", "POST", referer=ref)[0]
        assert st == 403, ref
    assert os.path.exists(os.path.join(card, "Roms/GBA/Celeste Classic (World).gba"))


def test_web_short_upload_leaves_nothing(env, card):
    e = dict(env, REQUEST_METHOD="POST", QUERY_STRING="op=put&path=Roms/GBA&name=cut.gba", HTTP_HOST=HOST,
             HTTP_REFERER="http://%s/" % HOST, CONTENT_LENGTH="1000")
    r = subprocess.run([CTL, "webcgi"], env=e, input=b"only 20 bytes here..", capture_output=True, timeout=60)
    assert b" 500 " in r.stdout.split(b"\r\n")[0]
    assert not [n for n in os.listdir(os.path.join(card, "Roms/GBA")) if "cut" in n]


def test_web_status_and_page_present(env, card):
    out = ctl(env, "web", "status").stdout
    assert "available=1" in out and "running=0" in out
    page = read(os.path.join(card, "TriMux/share/web/index.html"))
    assert "cgi-bin/files" in page and "Sem senha" in page


# ---------------------------------------------------------------- cleanup

def junk(card):
    write(os.path.join(card, "Roms/GBA/._Celeste Classic (World).gba"), "x" * 4096)
    write(os.path.join(card, "Roms/.DS_Store"), "x" * 100)
    write(os.path.join(card, "Imgs/GBA/Thumbs.db"), "x")
    write(os.path.join(card, ".Trashes/501/old.gba"), "x" * 10)
    write(os.path.join(card, "TriMux/._keep"), "system folder: never scanned")
    write(os.path.join(card, "TriMuxData/logs/trimux.log.1"), "old log")
    write(os.path.join(card, "TriMuxData/logs/perf/sessions.csv"), "a,b\n")
    write(os.path.join(card, ".trimux-store-new/half/file"), "partial")
    write(os.path.join(card, "TriMuxData/.store-download.zip"), "partial")
    write(os.path.join(card, "TriMux.old/VERSION"), "0.4.8")             # rollback copy: never touched
    write(os.path.join(card, "Saves/GBA/game.srm"), "save")


def test_clean_scan_changes_nothing_and_run_removes_only_junk(env, card):
    junk(card)
    out = ctl(env, "clean", "scan").stdout
    lines = {l.split("\t")[0]: l.split("\t") for l in out.splitlines() if "\t" in l}
    assert lines["computer"][1] == "4" and lines["logs"][1] == "2" and lines["temp"][1] == "2"
    assert os.path.exists(os.path.join(card, "Roms/GBA/._Celeste Classic (World).gba"))
    ctl(env, "clean", "run", "computer", "temp")
    for gone in ("Roms/GBA/._Celeste Classic (World).gba", "Roms/.DS_Store", "Imgs/GBA/Thumbs.db", ".Trashes",
                 ".trimux-store-new", "TriMuxData/.store-download.zip"):
        assert not os.path.exists(os.path.join(card, gone)), gone
    for kept in ("Roms/GBA/Celeste Classic (World).gba", "TriMux/._keep", "TriMux.old/VERSION",
                 "Saves/GBA/game.srm", "TriMuxData/logs/trimux.log.1", "TriMuxData/logs/perf/sessions.csv"):
        assert os.path.exists(os.path.join(card, kept)), kept
    ctl(env, "clean", "run", "logs")
    assert not os.path.exists(os.path.join(card, "TriMuxData/logs/trimux.log.1"))
    assert os.path.exists(os.path.join(card, "TriMuxData/logs/trimux.log"))
    assert ctl(env, "clean", "run", "games", check=False).returncode != 0


# ---------------------------------------------------------------- statistics

PLAYS = ("[plays]\nRoms/GBA/Celeste Classic (World).gba = 4 7200 1700000000\n"
         "Roms/FC/Micro Mages (World).nes = 1 300 1800000000\n")


def test_stats_command(env, card):
    write(os.path.join(card, "TriMuxData/state/plays.ini"), PLAYS)
    out = ctl(env, "stats").stdout
    assert out.splitlines()[0] == "seconds=7500 times=5 games=2"
    assert "top\t7200\t4\tRoms/GBA/Celeste Classic (World).gba" in out
    assert "system\tGBA\t7200\t1" in out


# ---------------------------------------------------------------- menus

# Home: ... Aplicativos, Configurações -> UP,UP. Aplicativos: [Ferramentas] Estatísticas, Jogo surpresa,
# Gerenciador de arquivos, Arquivos pelo navegador, Limpeza do cartão, [instalados] ...
APPS = "UP,UP,A"


@needs_ui
def test_surprise_game_opens_a_game(env, card):
    r = ui(env, APPS + ",DOWN,A,A")
    assert r.returncode == 10
    rom = [l for l in read(os.path.join(env["TRIMUX_TMP"], "launch.ini")).splitlines() if l.startswith("rom = ")][0]
    assert os.path.exists(os.path.join(card, rom[6:]))


@needs_ui
def test_stats_last_game_opens_its_options(env, card):
    write(os.path.join(card, "TriMuxData/state/plays.ini"), PLAYS)
    # Estatísticas: Tempo total, Sessões, Jogos diferentes, Último jogo -> A (opções) -> A (Jogar)
    r = ui(env, APPS + ",A,DOWN,DOWN,DOWN,A,A")
    assert r.returncode == 10
    assert "rom = Roms/FC/Micro Mages (World).nes" in read(os.path.join(env["TRIMUX_TMP"], "launch.ini"))


@needs_ui
def test_file_manager_deletes_after_confirmation(env, card):
    write(os.path.join(card, "AAA/zz.txt"), "bye")
    write(os.path.join(card, "AAA/zzz.txt"), "stay")
    # root: AAA, Roms, TriMux, TriMuxData -> AAA -> zz.txt -> A (dialog, "Não" selected) -> B keeps it
    r = ui(env, APPS + ",DOWN,DOWN,A,A,A,B,B,B,B")
    assert r.returncode == 0 and os.path.exists(os.path.join(card, "AAA/zz.txt"))
    r = ui(env, APPS + ",DOWN,DOWN,A,A,A,LEFT,A,B,B,B")
    assert r.returncode == 0
    assert not os.path.exists(os.path.join(card, "AAA/zz.txt"))
    assert read(os.path.join(card, "AAA/zzz.txt")) == "stay"


@needs_ui
def test_file_manager_cannot_delete_in_system_folders(env, card):
    # root: Roms, TriMux, TriMuxData -> TriMux -> first entry, A shows the read-only notice only
    r = ui(env, APPS + ",DOWN,DOWN,A,DOWN,A,A,LEFT,A,A,B,B,B")
    assert r.returncode == 0
    assert sorted(os.listdir(os.path.join(card, "TriMux"))) == sorted(["VERSION", "retroarch", "share"])


@needs_ui
def test_cleanup_page_removes_computer_files(env, card):
    junk(card)
    # Limpeza: O que foi encontrado, Arquivos do computador (marcado), Registros, Restos (marcado), Limpar agora
    r = ui(env, APPS + ",DOWN,DOWN,DOWN,DOWN,A,DOWN,DOWN,DOWN,DOWN,A,LEFT,A,B,B")
    assert r.returncode == 0
    assert not os.path.exists(os.path.join(card, "Roms/GBA/._Celeste Classic (World).gba"))
    assert not os.path.exists(os.path.join(card, ".trimux-store-new"))
    assert os.path.exists(os.path.join(card, "TriMuxData/logs/trimux.log.1"))    # not selected
    assert os.path.exists(os.path.join(card, "Roms/GBA/Celeste Classic (World).gba"))
    assert "clean: computer removed 4 files" in read(os.path.join(card, "TriMuxData/logs/trimux.log"))


@needs_ui
def test_web_server_needs_wifi(env, card):
    r = ui(env, APPS + ",DOWN,DOWN,DOWN,A,A,B,B")
    assert r.returncode == 0
    assert not os.path.exists(os.path.join(env["TRIMUX_TMP"], "web.pid"))


@needs_ui
def test_cover_grid_moves_by_card_and_row(env, card):
    """Grade de capas: the d-pad moves one card left/right and one row up/down."""
    write(os.path.join(card, "TriMuxData/config/trimux.ini"), "[general]\nwizard_done = 1\ngames_view = grid\n")
    for i in range(7):
        write(os.path.join(card, "Roms/GBA/Zz Game %d (World).gba" % i), "")
    # Todos os jogos, sorted by name: 5 of the test card first, then Zz Game 0..6
    r = ui(env, "DOWN,A,DOWN,RIGHT,A")             # row 2, column 2 -> 7th game = Zz Game 1
    assert r.returncode == 10
    assert "rom = Roms/GBA/Zz Game 1 (World).gba" in read(os.path.join(env["TRIMUX_TMP"], "launch.ini"))

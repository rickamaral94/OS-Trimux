"""App store (trimuxctl store, Aplicativos › Loja) and PortMaster in Ports.

Downloads are simulated: the fake curl serves zip files built here from URLs
mapped in netstate/serve, and the PortMaster pak is a stand-in whose
launch.sh only records how TriMux started it. This checks the store's
decisions and file operations, not the real projects, the network or the
device."""
import hashlib
import io
import os
import shutil
import time
import zipfile

import pytest

from conftest import CTL, ctl, read, write
from test_ui import _ui_runs, ui

needs_ui = pytest.mark.skipif(not _ui_runs(), reason="build/native/trimux-ui not built for this system")
URL_PM = "https://github.com/example/minui-portmaster/releases/download/9.9/PORTS.pak.zip"
URL_GR = "https://github.com/example/grout/releases/download/v1/Grout-Trimui.zip"


def make_zip(path, entries):
    """entries: name -> (bytes, unix mode) ; names ending in / are folders."""
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, (data, mode) in entries.items():
            info = zipfile.ZipInfo(name)
            info.create_system = 3
            info.external_attr = (mode | (0o040000 if name.endswith("/") else 0o100000)) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    return path


PAK_LAUNCH = b"""#!/bin/sh
d="$(dirname "$0")"
{ echo "arg=$1"; echo "sd=$SDCARD_PATH"; echo "platform=$PLATFORM"; echo "user=$USERDATA_PATH"; echo "logs=$LOGS_PATH"; } > "$d/started"
exit 0
"""


def pak_zip(tmp, gui="v1"):
    return make_zip(os.path.join(tmp, "pak-%s.zip" % gui), {
        "launch.sh": (PAK_LAUNCH, 0o644),
        "files/": (b"", 0o755),
        "files/progressor": (b"#!/bin/sh\n", 0o755),
        "PortMaster/": (b"", 0o755),
        "PortMaster/pugwash": (b"gui " + gui.encode() + b"\n" * 2000, 0o755),
        "LICENSE": (b"MIT\n", 0o644),
    })


def grout_zip(tmp):
    return make_zip(os.path.join(tmp, "grout.zip"), {
        "Grout/": (b"", 0o755),
        "Grout/config.json": (b'{"label":"Grout","launch":"launch.sh","description":"RomM"}', 0o644),
        "Grout/launch.sh": (b"#!/bin/sh\n", 0o755),
        "Grout/grout/grout": (os.urandom(200000), 0o755),
    })


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def store_ini(card, pm, gr, pm_hash=None):
    write(os.path.join(card, "TriMux/share/store.ini"), """
[portmaster]
name = PortMaster
desc = store.portmaster.desc
version = 9.9
url = %s
sha256 = %s
size = %d
license = MIT
source = https://github.com/example/minui-portmaster
dest = Emus/tg5040/PORTS.pak
installed = launch.sh
keep = PortMaster
remove = Emus/tg5040/PORTS.pak|Roms/PORTS/Portmaster.sh
touch = Roms/PORTS/Portmaster.sh
system = 1

[grout]
name = Grout
desc = store.grout.desc
version = 1
url = %s
sha256 = %s
size = %d
license = MIT
source = https://github.com/example/grout
dest = Apps
installed = Grout/launch.sh
remove = Apps/Grout

[evil]
name = Evil
version = 1
url = %s
sha256 = %s
size = 10
dest = Saves
installed = x
""" % (URL_PM, pm_hash or sha(pm), os.path.getsize(pm), URL_GR, sha(gr), os.path.getsize(gr), URL_GR, sha(gr)))


@pytest.fixture
def st(env, card, tmp_path):
    dev = env["TRIMUX_SYSFS_ROOT"]
    write(os.path.join(dev, "run/wpa_supplicant"), "")
    write(os.path.join(dev, "netstate/status"), "wpa_state=COMPLETED\nssid=Casa\nip_address=127.0.0.1\n")
    pm, gr = pak_zip(str(tmp_path)), grout_zip(str(tmp_path))
    write(os.path.join(dev, "netstate/serve"), "%s\t%s\n%s\t%s\n" % (URL_PM, pm, URL_GR, gr))
    store_ini(card, pm, gr)
    return {"env": env, "card": card, "pm": pm, "gr": gr, "tmp": str(tmp_path)}


def status(env):
    s = read(os.path.join(env["TRIMUX_TMP"], "store.status"))
    return dict(l.split(" = ", 1) for l in s.splitlines() if " = " in l)


def test_list_rejects_entries_outside_the_allowed_folders(st):
    out = ctl(st["env"], "store", "list").stdout
    assert "portmaster\t9.9\tavailable" in out and "grout\t1\tavailable" in out
    assert "evil" not in out                       # dest = Saves is never allowed


def test_install_verify_and_remove_portmaster(st):
    env, card = st["env"], st["card"]
    write(os.path.join(card, "Roms/PORTS/.ports/stardew/data.bin"), "user port data")
    assert ctl(env, "store", "install", "portmaster").returncode == 0
    pak = os.path.join(card, "Emus/tg5040/PORTS.pak")
    assert os.path.exists(os.path.join(pak, "launch.sh"))
    assert os.access(os.path.join(pak, "files/progressor"), os.X_OK)   # permissions kept from the zip
    assert os.path.exists(os.path.join(card, "Roms/PORTS/Portmaster.sh"))
    assert status(env)["state"] == "done"
    assert not os.path.exists(os.path.join(card, ".trimux-store-new"))
    assert not os.path.exists(os.path.join(card, "TriMuxData/.store-download.zip"))
    assert "installed" in ctl(env, "store", "list").stdout.split("\n")[0]
    # remove: only the pak and its entry; installed ports stay
    assert ctl(env, "store", "remove", "portmaster").returncode == 0
    assert not os.path.exists(pak) and not os.path.exists(os.path.join(card, "Roms/PORTS/Portmaster.sh"))
    assert read(os.path.join(card, "Roms/PORTS/.ports/stardew/data.bin")) == "user port data"


def test_update_keeps_the_users_portmaster_folder(st):
    env, card = st["env"], st["card"]
    assert ctl(env, "store", "install", "portmaster").returncode == 0
    write(os.path.join(card, "Emus/tg5040/PORTS.pak/PortMaster/config/config.json"), "mine")
    pm2 = pak_zip(st["tmp"], "v2")
    write(os.path.join(env["TRIMUX_SYSFS_ROOT"], "netstate/serve"), "%s\t%s\n%s\t%s\n" % (URL_PM, pm2, URL_GR, st["gr"]))
    store_ini(card, pm2, st["gr"])
    assert ctl(env, "store", "install", "portmaster").returncode == 0
    assert read(os.path.join(card, "Emus/tg5040/PORTS.pak/PortMaster/config/config.json")) == "mine"


def test_checksum_mismatch_writes_nothing(st):
    env, card = st["env"], st["card"]
    store_ini(card, st["pm"], st["gr"], pm_hash="0" * 64)
    assert ctl(env, "store", "install", "portmaster", check=False).returncode != 0
    assert status(env)["state"] == "error" and status(env)["error"] == "checksum"
    assert not os.path.exists(os.path.join(card, "Emus"))
    assert not os.path.exists(os.path.join(card, "TriMuxData/.store-download.zip"))


def test_zip_escaping_the_folder_is_refused(st):
    env, card = st["env"], st["card"]
    bad = make_zip(os.path.join(st["tmp"], "bad.zip"), {"launch.sh": (b"#!/bin/sh\n", 0o755),
                                                         "../../Saves/pwned": (b"x", 0o644)})
    write(os.path.join(env["TRIMUX_SYSFS_ROOT"], "netstate/serve"), "%s\t%s\n%s\t%s\n" % (URL_PM, bad, URL_GR, st["gr"]))
    store_ini(card, bad, st["gr"])
    assert ctl(env, "store", "install", "portmaster", check=False).returncode != 0
    assert status(env)["error"] == "package"
    assert not os.path.exists(os.path.join(card, "Saves/pwned")) and not os.path.exists(os.path.join(card, "Emus"))


def test_no_wifi_and_low_space_refuse_before_download(st):
    env = st["env"]
    os.unlink(os.path.join(env["TRIMUX_SYSFS_ROOT"], "run/wpa_supplicant"))
    assert ctl(env, "store", "install", "grout", check=False).returncode != 0
    assert status(env)["error"] == "nowifi"


def test_grout_lands_in_apps_and_is_listed(st):
    env, card = st["env"], st["card"]
    assert ctl(env, "store", "install", "grout").returncode == 0
    assert os.path.exists(os.path.join(card, "Apps/Grout/config.json"))
    assert ctl(env, "store", "remove", "grout").returncode == 0
    assert not os.path.exists(os.path.join(card, "Apps/Grout")) and os.path.isdir(os.path.join(card, "Apps"))


def test_portmaster_entry_starts_the_pak_with_minui_environment(st):
    env, card = st["env"], st["card"]
    assert ctl(env, "store", "install", "portmaster").returncode == 0
    os.makedirs(env["TRIMUX_TMP"], exist_ok=True)
    write(os.path.join(env["TRIMUX_TMP"], "launch.ini"),
          "[launch]\nsystem = PORTS\nrom = Roms/PORTS/Portmaster.sh\nemulator = portmaster\n")
    ctl(env, "launch", check=False)
    started = read(os.path.join(card, "Emus/tg5040/PORTS.pak/started"))
    assert "arg=%s/Roms/PORTS/Portmaster.sh" % card in started and "platform=tg5040" in started
    assert "sd=%s" % card in started and "user=%s/TriMuxData/portmaster" % card in started


@needs_ui
def test_ports_list_opens_portmaster_scripts_through_portmaster(st):
    """A PortMaster port (it uses $controlfolder) goes to PortMaster; a plain
    script keeps the shell launcher."""
    env, card = st["env"], st["card"]
    assert ctl(env, "store", "install", "portmaster").returncode == 0
    write(os.path.join(card, "Roms/PORTS/Zzz Mine.sh"), "#!/bin/sh\necho mine\n", 0o755)
    os.unlink(os.path.join(card, "Roms/PORTS/Portmaster.sh"))
    write(os.path.join(card, "Roms/PORTS/Celeste.sh"), "#!/bin/bash\ncontrolfolder=/x\n", 0o755)
    # "Celeste" (Ports) sorts before every other game of the test card
    req = os.path.join(env["TRIMUX_TMP"], "launch.ini")
    write(os.path.join(card, "TriMuxData/config/trimux.ini"), "[general]\nwizard_done = 1\n")
    r = ui(env, "DOWN,A,A,LEFT,A")                # Todos os jogos -> first game, accept the "experimental" note
    assert r.returncode == 10
    text = read(req)
    assert "Roms/PORTS/Celeste.sh" in text and "emulator = portmaster" in text


@needs_ui
def test_store_in_apps_page_installs_after_confirmation(st):
    env, card = st["env"], st["card"]
    os.makedirs(os.path.join(card, "TriMux/bin"), exist_ok=True)
    shutil.copy(CTL, os.path.join(card, "TriMux/bin/trimuxctl"))
    os.chmod(os.path.join(card, "TriMux/bin/trimuxctl"), 0o755)
    # home: ... Aplicativos, Configurações -> UP,UP; rows: "Nenhum aplicativo", PortMaster, Grout
    r = ui(env, "UP,UP,A,DOWN,DOWN,A,LEFT,A,wait=1500,wait=1500,B,B")
    assert r.returncode == 0
    for _ in range(20):
        if os.path.exists(os.path.join(card, "Apps/Grout/config.json")):
            break
        time.sleep(0.25)
    assert os.path.exists(os.path.join(card, "Apps/Grout/config.json"))

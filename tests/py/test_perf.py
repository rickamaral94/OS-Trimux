"""Performance on heavy emulators (N64, Dreamcast, PSP): recommended core
settings for this device written once, and the opt-in 2.0 GHz limit, on a
simulated Brick Pro. Speed itself can only be measured on the device."""
import os

from conftest import read, write
from test_supervisor import rig, run_supervisor, scripted_ui  # noqa: F401  (fixture)

N64_ROM = "Roms/N64/Homebrew (World).z64"


def launch_n64(rig, settings=""):  # noqa: F811
    write(os.path.join(rig["tm"], "retroarch/cores/mupen64plus_next_libretro.so"), "fake core\n")
    write(os.path.join(rig["card"], N64_ROM), "x")
    os.makedirs(rig["tmp"], exist_ok=True)
    write(os.path.join(rig["tmp"], "launch.ini"),
          "[launch]\nsystem = N64\nrom = %s\nemulator = mupen64plus_next\n" % N64_ROM)
    if settings:
        write(os.path.join(rig["card"], "TriMuxData/config/trimux.ini"), settings)
    scripted_ui(rig, [10, 20])
    run_supervisor(rig)


OPT = "TriMuxData/retroarch/config/Mupen64Plus-Next/Mupen64Plus-Next.opt"


def test_heavy_emulators_stay_at_18_ghz_by_default(rig):  # noqa: F811
    launch_n64(rig)
    assert read(os.path.join(rig["dir"], "ra_maxfreq")) == "1800000"


def test_heavy_emulators_use_20_ghz_when_allowed(rig):  # noqa: F811
    launch_n64(rig, "[power]\nheavy_boost = 1\n")
    assert read(os.path.join(rig["dir"], "ra_maxfreq")) == "2000000"


def test_heavy_boost_follows_only_the_automatic_profile(rig):  # noqa: F811
    launch_n64(rig, "[power]\nheavy_boost = 1\nprofile = balanced\n")
    assert read(os.path.join(rig["dir"], "ra_maxfreq")) == "1608000"


def test_recommended_settings_written_once_and_user_changes_kept(rig):  # noqa: F811
    opt = os.path.join(rig["card"], OPT)
    write(opt, 'mupen64plus-cpucore = "dynamic_recompiler"\nmupen64plus-EnableNativeResFactor = "0"\n')
    launch_n64(rig)
    text = read(opt)
    assert 'mupen64plus-EnableNativeResFactor = "1"' in text and 'mupen64plus-cpucore = "dynamic_recompiler"' in text
    assert "[tuned]" in read(os.path.join(rig["card"], "TriMuxData/state/tuned.ini"))
    log = read(os.path.join(rig["card"], "TriMuxData/logs/trimux.log"))
    assert "recommended settings for this device applied to Mupen64Plus-Next" in log
    # the user goes back to 640x480 in RetroArch's menu: the next launch keeps it
    write(opt, read(opt).replace('EnableNativeResFactor = "1"', 'EnableNativeResFactor = "0"'))
    launch_n64(rig)
    assert 'mupen64plus-EnableNativeResFactor = "0"' in read(opt)


def test_trimux_resolution_choice_wins_over_the_recommendation(rig):  # noqa: F811
    launch_n64(rig, "[video.N64]\nres = 2\n")
    assert 'mupen64plus-EnableNativeResFactor = "2"' in read(os.path.join(rig["card"], OPT))


def test_dreamcast_and_psp_recommended_settings(rig):  # noqa: F811
    for core, sysid, emu, rom, cfg in (("flycast", "DC", "flycast", "Roms/DC/Demo.cdi", "Flycast"),
                                       ("ppsspp", "PSP", "ppsspp", "Roms/PSP/Demo.iso", "PPSSPP")):
        write(os.path.join(rig["tm"], "retroarch/cores/%s_libretro.so" % core), "fake core\n")
        write(os.path.join(rig["card"], rom), "x")
        os.makedirs(rig["tmp"], exist_ok=True)
        write(os.path.join(rig["tmp"], "launch.ini"),
              "[launch]\nsystem = %s\nrom = %s\nemulator = %s\n" % (sysid, rom, emu))
        scripted_ui(rig, [10, 20])
        run_supervisor(rig)
    dc = read(os.path.join(rig["card"], "TriMuxData/retroarch/config/Flycast/Flycast.opt"))
    assert 'reicast_anisotropic_filtering = "off"' in dc and 'reicast_enable_dsp = "disabled"' in dc
    assert 'reicast_alpha_sorting = "per-strip (fast, least accurate)"' in dc
    psp = read(os.path.join(rig["card"], "TriMuxData/retroarch/config/PPSSPP/PPSSPP.opt"))
    assert 'ppsspp_texture_anisotropic_filtering = "disabled"' in psp


# ---- the switch in Configurações › Energia (profile is the first row, this the second)
import pytest  # noqa: E402

from test_ui import _ui_runs, ui  # noqa: E402

POWER = "UP,A,DOWN,DOWN,DOWN,A"


@pytest.mark.skipif(not _ui_runs(), reason="build/native/trimux-ui not built for this system")
def test_heavy_boost_switch_asks_first(env, tmp_path):
    cfg = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini")
    write(cfg, "[general]\nwizard_done = 1\n")
    # A asks; the answer comes on "Não": nothing changes
    assert ui(env, POWER + ",DOWN,A,A,B,B").returncode == 0
    assert "heavy_boost = 1" not in read(cfg)
    # "Sim" turns it on; A again turns it off without asking
    assert ui(env, POWER + ",DOWN,A,LEFT,A,B,B").returncode == 0
    assert "heavy_boost = 1" in read(cfg)
    assert ui(env, POWER + ",DOWN,A,B,B").returncode == 0
    assert "heavy_boost = 0" in read(cfg)


# ---- home screen as a carousel (Configurações › Aparência › Tela inicial)

@pytest.mark.skipif(not _ui_runs(), reason="build/native/trimux-ui not built for this system")
def test_home_carousel_moves_sideways_and_opens(env, tmp_path):
    cfg = os.path.join(env["TRIMUX_SDCARD"], "TriMuxData/config/trimux.ini")
    write(cfg, "[general]\nwizard_done = 1\nhome_view = carousel\n")
    shot = str(tmp_path / "carousel.bmp")
    # Favoritos, Todos os jogos, NES, GB, GBA, PS1, Ports...: RIGHT twice -> NES, A opens its list
    r = ui(env, "RIGHT,RIGHT,shot=%s,A,A" % shot)                # NES -> its list -> play the first game
    assert r.returncode == 10 and os.path.getsize(shot) > 100000
    assert "system = FC" in read(os.path.join(env["TRIMUX_TMP"], "launch.ini"))

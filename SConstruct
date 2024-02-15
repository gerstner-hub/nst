import sys
from pathlib import Path

try:
    # if there is already an environment then simply use that, some other
    # level of the build system already initialized it
    Import('env')
except Exception:
    try:
        from buildsystem import initSCons
    except ImportError:
        cosmos_scripts = Path(Dir('.').abspath) / "libcosmos" / "scripts"
        sys.path.append(str(cosmos_scripts))
        from buildsystem import initSCons
    # prefer static linking given the ABI issues in libcosmos and libxpp
    env = initSCons("nst", rtti=False, deflibtype="static")
    env['install_dev_files'] = False

env.AddLocalLibrary("libcosmos")
env.AddLocalLibrary("libxpp")

SConscript(env['buildroot'] + 'src/SConstruct')
SConscript(env['buildroot'] + 'doc/SConstruct')
SConscript(env['buildroot'] + 'data/SConstruct')

Default(env['bins']['nst'])

instroot = Path(env['instroot'])

nst_inst_node = env.Install(instroot / "bin", env['bins']['nst'])
nst_msg_inst_node = env.Install(instroot / "bin", env['bins']['nst-msg'])
env.Alias("install", nst_inst_node)
env.Alias("install", nst_msg_inst_node)
man = env.Install(instroot / "share" / "man" / "man1", "#doc/nst.1")
env.Alias("install", man)
terminfo = env.Install(instroot / "share", env['buildroot'] + "/data/terminfo")
env.Alias("install", terminfo)
desktop_entry = env.Install(instroot / "share" / "applications", "#data/nst.desktop")
env.Alias("install", desktop_entry)

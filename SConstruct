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
    env = initSCons("nst")

cosmos_env = env.Clone()
cosmos_env['buildroot'] = ""
SConscript('libcosmos/SConstruct', duplicate=0, variant_dir=env['buildroot'] + "libcosmos/", exports={"env": cosmos_env})
xpp_env = env.Clone()
xpp_env['buildroot'] = ""
SConscript('libX++/SConstruct', duplicate=0, variant_dir=env['buildroot'] + "libX++/", exports={"env": xpp_env})

SConscript(env['buildroot'] + 'src/SConstruct')
#SConscript(env['buildroot'] + 'test/SConstruct')
Default(env['bins']['nst'])
